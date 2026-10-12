#include "desktop/windows/video/presenter.hpp"
#include "renderer/video/settings.hpp"
#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <stdexcept>
#include <wrl/client.h>

namespace cupid::desktop {
namespace {
using Microsoft::WRL::ComPtr;

void require(HRESULT result, const char *description) {
  if (FAILED(result))
    throw std::runtime_error(description);
}

LRESULT CALLBACK video_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCHITTEST)
    return HTTRANSPARENT;
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);
    EndPaint(window, &paint);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

constexpr char shaders[] = R"(
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vertex(uint index : SV_VertexID) {
  Vertex output;
  output.uv = float2((index << 1) & 2, index & 2);
  output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return output;
}
Texture2D<float4> Image : register(t0);
SamplerState Pixels : register(s0);
float4 pixel(Vertex input) : SV_Target { return Image.Sample(Pixels, input.uv); }
)";

ComPtr<ID3DBlob> shader(const char *entry, const char *target) {
  ComPtr<ID3DBlob> code, errors;
  require(D3DCompile(shaders, sizeof(shaders) - 1, nullptr, nullptr, nullptr, entry, target,
                     D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                     &errors),
          "Could not compile the video presentation shader.");
  return code;
}
} // namespace

struct VideoPresenter::Implementation {
  HWND window = nullptr;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGISwapChain> chain;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> image;
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11SamplerState> sampler;
  unsigned width = 0, height = 0, image_width = 0, image_height = 0;
  Viewport viewport{};
  std::string error;
  bool attempted = false;

  ~Implementation() {
    if (context)
      context->ClearState();
    target.Reset();
    chain.Reset();
    if (IsWindow(window))
      DestroyWindow(window);
  }

  void initialize(HWND parent, bool software_device) {
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"CupidVideoSurface";
    type.lpfnWndProc = video_message;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error("Could not register the video surface.");
    window = CreateWindowW(type.lpszClassName, L"", WS_CHILD, 0, 0, 1, 1, parent, nullptr,
                           type.hInstance, nullptr);
    if (!window)
      throw std::runtime_error("Could not create the video surface.");
    ComPtr<IDXGIFactory1> factory;
    require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Could not create the video factory.");
    ComPtr<IDXGIAdapter1> selected;
    SIZE_T memory = 0;
    if (!software_device) {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
          break;
        DXGI_ADAPTER_DESC1 description{};
        if (adapter && SUCCEEDED(adapter->GetDesc1(&description)) &&
            !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            (!selected || description.DedicatedVideoMemory > memory)) {
          selected = adapter;
          memory = description.DedicatedVideoMemory;
        }
      }
    }
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                     D3D_FEATURE_LEVEL_10_0};
    require(D3D11CreateDevice(selected.Get(),
                              software_device ? D3D_DRIVER_TYPE_WARP
                              : selected      ? D3D_DRIVER_TYPE_UNKNOWN
                                              : D3D_DRIVER_TYPE_HARDWARE,
                              nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                              static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device,
                              nullptr, &context),
            "Could not initialize GPU video presentation.");
    const auto vs = shader("vertex", "vs_4_0"), ps = shader("pixel", "ps_4_0");
    require(
        device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex),
        "Could not create the video vertex shader.");
    require(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel),
            "Could not create the video pixel shader.");
    D3D11_SAMPLER_DESC sampling{};
    sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampling.MaxLOD = D3D11_FLOAT32_MAX;
    require(device->CreateSamplerState(&sampling, &sampler), "Could not create the pixel sampler.");
    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferDesc.Width = swap.BufferDesc.Height = 1;
    swap.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap.SampleDesc.Count = 1;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.BufferCount = 2;
    swap.OutputWindow = window;
    swap.Windowed = TRUE;
    swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    require(factory->CreateSwapChain(device.Get(), &swap, &chain),
            "Could not create the video swap chain.");
    factory->MakeWindowAssociation(parent, DXGI_MWA_NO_ALT_ENTER);
    ComPtr<IDXGIDevice1> latency;
    if (SUCCEEDED(device.As(&latency)))
      latency->SetMaximumFrameLatency(1);
  }

  void resize(unsigned next_width, unsigned next_height) {
    if (width == next_width && height == next_height)
      return;
    context->OMSetRenderTargets(0, nullptr, nullptr);
    target.Reset();
    require(chain->ResizeBuffers(0, next_width, next_height, DXGI_FORMAT_UNKNOWN, 0),
            "Could not resize the video surface.");
    ComPtr<ID3D11Texture2D> buffer;
    require(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Could not obtain the video back buffer.");
    require(device->CreateRenderTargetView(buffer.Get(), nullptr, &target),
            "Could not create the video render target.");
    width = next_width;
    height = next_height;
    SetWindowPos(window, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void upload(std::span<const std::uint8_t> pixels, unsigned next_width, unsigned next_height) {
    if (image_width != next_width || image_height != next_height) {
      ID3D11ShaderResourceView *unbound = nullptr;
      context->PSSetShaderResources(0, 1, &unbound);
      image.Reset();
      texture.Reset();
      D3D11_TEXTURE2D_DESC description{};
      description.Width = next_width;
      description.Height = next_height;
      description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
      description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      description.Usage = D3D11_USAGE_DEFAULT;
      description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      require(device->CreateTexture2D(&description, nullptr, &texture),
              "Could not allocate the video image.");
      require(device->CreateShaderResourceView(texture.Get(), nullptr, &image),
              "Could not create the video image view.");
      image_width = next_width;
      image_height = next_height;
    }
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), next_width * 4, 0);
  }

  void draw() {
    const float black[]{0, 0, 0, 1};
    context->ClearRenderTargetView(target.Get(), black);
    const D3D11_VIEWPORT area{static_cast<float>(viewport.x),
                              static_cast<float>(viewport.y),
                              static_cast<float>(viewport.width),
                              static_cast<float>(viewport.height),
                              0,
                              1};
    context->RSSetViewports(1, &area);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, image.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    context->Draw(3, 0);
  }
};

VideoPresenter::VideoPresenter() : implementation_(std::make_unique<Implementation>()) {}
VideoPresenter::~VideoPresenter() = default;

bool VideoPresenter::initialize(HWND parent, bool software_device) {
  auto &state = *implementation_;
  if (state.attempted)
    return available();
  state.attempted = true;
  try {
    state.initialize(parent, software_device);
    return true;
  } catch (const std::exception &exception) {
    state.error = exception.what();
    if (IsWindow(state.window))
      ShowWindow(state.window, SW_HIDE);
    return false;
  }
}

bool VideoPresenter::present(std::span<const std::uint8_t> pixels, unsigned width, unsigned height,
                             unsigned viewport_width, unsigned viewport_height, DisplayScale scale,
                             DisplayAspect aspect) {
  if (!available())
    return false;
  auto &state = *implementation_;
  try {
    if (!width || !height || !viewport_width || !viewport_height ||
        width > n64::maximum_frame_width || height > n64::maximum_frame_height ||
        pixels.size() != std::size_t(width) * height * 4)
      throw std::runtime_error("The video presentation image has invalid dimensions.");
    state.resize(viewport_width, viewport_height);
    state.upload(pixels, width, height);
    state.viewport = image_viewport(width, height, viewport_width, viewport_height, scale, aspect);
    state.draw();
    ShowWindow(state.window, SW_SHOWNOACTIVATE);
    require(state.chain->Present(0, 0), "GPU video presentation failed.");
    return true;
  } catch (const std::exception &exception) {
    state.error = exception.what();
    ShowWindow(state.window, SW_HIDE);
    return false;
  }
}

std::vector<std::uint8_t> VideoPresenter::capture() {
  auto &state = *implementation_;
  if (!available() || !state.target || !state.image)
    return {};
  state.draw();
  ComPtr<ID3D11Texture2D> buffer, staging;
  require(state.chain->GetBuffer(0, IID_PPV_ARGS(&buffer)),
          "Could not obtain the captured surface.");
  D3D11_TEXTURE2D_DESC description{};
  buffer->GetDesc(&description);
  description.Usage = D3D11_USAGE_STAGING;
  description.BindFlags = 0;
  description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  description.MiscFlags = 0;
  require(state.device->CreateTexture2D(&description, nullptr, &staging),
          "Could not create the presentation capture.");
  state.context->CopyResource(staging.Get(), buffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  require(state.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
          "Could not read the presentation capture.");
  std::vector<std::uint8_t> pixels(std::size_t(state.width) * state.height * 4);
  for (unsigned row = 0; row < state.height; ++row)
    std::memcpy(pixels.data() + std::size_t(row) * state.width * 4,
                static_cast<const std::uint8_t *>(mapped.pData) +
                    std::size_t(row) * mapped.RowPitch,
                std::size_t(state.width) * 4);
  state.context->Unmap(staging.Get(), 0);
  return pixels;
}

bool VideoPresenter::available() const {
  return implementation_->chain && implementation_->error.empty();
}

const std::string &VideoPresenter::error() const {
  return implementation_->error;
}

void VideoPresenter::hide() {
  if (IsWindow(implementation_->window))
    ShowWindow(implementation_->window, SW_HIDE);
}

} // namespace cupid::desktop
