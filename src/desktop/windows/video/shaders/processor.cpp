#include "desktop/windows/video/shaders/processor.hpp"
#include "renderer/video/settings.hpp"
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <wrl/client.h>

namespace cupid::desktop {
namespace {

using Microsoft::WRL::ComPtr;

void require(HRESULT result, const char *action) {
  if (FAILED(result))
    throw std::runtime_error(action);
}

constexpr char vertex_source[] = R"(
float4 main(uint vertex : SV_VertexID) : SV_Position {
  float2 corner = float2((vertex << 1) & 2, vertex & 2);
  return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
})";

constexpr char pixel_interface[] = R"(
Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);
cbuffer Dimensions : register(b0) { float4 SourceSize; };
float4 pixel(int2 position) {
  return SourceTexture.Load(int3(clamp(position, int2(0, 0), int2(SourceSize.xy) - 1), 0));
}
)";

std::string pixel_source(DisplayShader preset) {
  const std::string declarations = pixel_interface;
  if (preset == DisplayShader::Soft)
    return declarations + R"(
float4 main(float4 position : SV_Position) : SV_Target {
  int2 p = int2(position.xy);
  float4 color = pixel(p);
  color.rgb = (color.rgb * 4 + pixel(p + int2(-1, 0)).rgb + pixel(p + int2(1, 0)).rgb
             + pixel(p + int2(0, -1)).rgb + pixel(p + int2(0, 1)).rgb) / 8;
  return color;
})";
  return declarations + R"(
float4 main(float4 position : SV_Position) : SV_Target {
  float4 color = pixel(int2(position.xy));
  if ((uint(position.y) & 1) != 0) color.rgb *= 0.6;
  return color;
})";
}

ComPtr<ID3DBlob> compile(const char *source, std::size_t size, const char *target,
                         const std::filesystem::path &file = {}) {
  ComPtr<ID3DBlob> shader, diagnostics;
  const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
  const auto result =
      file.empty()
          ? D3DCompile(source, size, nullptr, nullptr, nullptr, "main", target, flags, 0,
                       shader.GetAddressOf(), diagnostics.GetAddressOf())
          : D3DCompileFromFile(file.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
                               target, flags, 0, shader.GetAddressOf(), diagnostics.GetAddressOf());
  if (FAILED(result)) {
    std::string detail = "Display shader compilation failed.";
    if (diagnostics)
      detail += " " + std::string(static_cast<const char *>(diagnostics->GetBufferPointer()),
                                  diagnostics->GetBufferSize());
    throw std::runtime_error(detail);
  }
  return shader;
}

} // namespace

struct ShaderProcessor::Implementation {
  ShaderSelection selection;
  bool attempted = false;
  std::string error;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Buffer> dimensions;
  ComPtr<ID3D11Texture2D> input, output, staging;
  ComPtr<ID3D11ShaderResourceView> input_view;
  ComPtr<ID3D11RenderTargetView> output_view;
  unsigned width = 0, height = 0;
  std::vector<std::uint8_t> filtered;

  void initialize() {
    if (selection.preset == DisplayShader::File &&
        !std::filesystem::is_regular_file(selection.file))
      throw std::runtime_error("The selected display shader file is missing or unreadable.");
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                     D3D_FEATURE_LEVEL_10_0};
    auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                    static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                    device.GetAddressOf(), nullptr, context.GetAddressOf());
    if (FAILED(result)) {
      device.Reset();
      context.Reset();
      result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
                                 static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                 device.GetAddressOf(), nullptr, context.GetAddressOf());
    }
    require(result, "Display shader device initialization failed.");
    const auto vs = compile(vertex_source, sizeof(vertex_source) - 1, "vs_4_0");
    require(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                       vertex.GetAddressOf()),
            "Could not create the display vertex shader.");
    const auto source = pixel_source(selection.preset);
    const auto ps =
        compile(source.data(), source.size(), "ps_4_0",
                selection.preset == DisplayShader::File ? selection.file : std::filesystem::path{});
    require(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                      pixel.GetAddressOf()),
            "Could not create the display pixel shader.");
    D3D11_SAMPLER_DESC sampling{};
    sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampling.MaxLOD = D3D11_FLOAT32_MAX;
    require(device->CreateSamplerState(&sampling, sampler.GetAddressOf()),
            "Could not create the display shader sampler.");
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(float) * 4;
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    require(device->CreateBuffer(&buffer, nullptr, dimensions.GetAddressOf()),
            "Could not create the display shader dimensions.");
  }

  void resize(unsigned selected_width, unsigned selected_height) {
    if (width == selected_width && height == selected_height)
      return;
    context->ClearState();
    input_view.Reset();
    output_view.Reset();
    input.Reset();
    output.Reset();
    staging.Reset();
    width = height = 0;
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = selected_width;
    texture.Height = selected_height;
    texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    require(device->CreateTexture2D(&texture, nullptr, input.GetAddressOf()),
            "Could not allocate the display shader input image.");
    require(device->CreateShaderResourceView(input.Get(), nullptr, input_view.GetAddressOf()),
            "Could not create the display shader input view.");
    texture.BindFlags = D3D11_BIND_RENDER_TARGET;
    require(device->CreateTexture2D(&texture, nullptr, output.GetAddressOf()),
            "Could not allocate the display shader output image.");
    require(device->CreateRenderTargetView(output.Get(), nullptr, output_view.GetAddressOf()),
            "Could not create the display shader output view.");
    texture.Usage = D3D11_USAGE_STAGING;
    texture.BindFlags = 0;
    texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require(device->CreateTexture2D(&texture, nullptr, staging.GetAddressOf()),
            "Could not allocate the display shader readback image.");
    width = selected_width;
    height = selected_height;
  }

  std::span<const std::uint8_t> render(const n64::VideoFrame &frame) {
    resize(frame.width, frame.height);
    context->UpdateSubresource(input.Get(), 0, nullptr, frame.rgba.data(), width * 4, 0);
    const float size[]{static_cast<float>(width), static_cast<float>(height), 1.0f / width,
                       1.0f / height};
    context->UpdateSubresource(dimensions.Get(), 0, nullptr, size, 0, 0);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height),
                                  0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, input_view.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, dimensions.GetAddressOf());
    context->OMSetRenderTargets(1, output_view.GetAddressOf(), nullptr);
    context->Draw(3, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView *unbound = nullptr;
    context->PSSetShaderResources(0, 1, &unbound);
    context->CopyResource(staging.Get(), output.Get());
    filtered.resize(frame.rgba.size());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Could not read the display shader output image.");
    for (unsigned row = 0; row < height; ++row)
      std::memcpy(filtered.data() + std::size_t(row) * width * 4,
                  static_cast<const std::uint8_t *>(mapped.pData) +
                      std::size_t(row) * mapped.RowPitch,
                  std::size_t(width) * 4);
    context->Unmap(staging.Get(), 0);
    return filtered;
  }
};

ShaderProcessor::ShaderProcessor() : implementation_(std::make_unique<Implementation>()) {}
ShaderProcessor::~ShaderProcessor() = default;

void ShaderProcessor::select(ShaderSelection selection) {
  if (!selection.valid())
    throw std::runtime_error("Select a supported display shader.");
  auto next = std::make_unique<Implementation>();
  next->selection = std::move(selection);
  implementation_ = std::move(next);
}

std::span<const std::uint8_t> ShaderProcessor::apply(const n64::VideoFrame &frame) {
  auto &state = *implementation_;
  if (state.selection.preset == DisplayShader::None || frame.rgba.empty())
    return frame.rgba;
  if (!state.error.empty())
    return frame.rgba;
  try {
    if (!frame.width || !frame.height || frame.width > n64::maximum_frame_width ||
        frame.height > n64::maximum_frame_height ||
        std::size_t(frame.width) * frame.height * 4 != frame.rgba.size())
      throw std::runtime_error("The display image dimensions are not supported by this shader.");
    if (!state.attempted) {
      state.attempted = true;
      state.initialize();
    }
    return state.render(frame);
  } catch (const std::exception &error) {
    state.error =
        std::string(error.what()) + " Unfiltered image shown; select None or reload the shader.";
    if (state.context)
      state.context->ClearState();
    return frame.rgba;
  }
}

const ShaderSelection &ShaderProcessor::selection() const {
  return implementation_->selection;
}

const std::string &ShaderProcessor::error() const {
  return implementation_->error;
}

} // namespace cupid::desktop
