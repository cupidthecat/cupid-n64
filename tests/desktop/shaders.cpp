#include "../support/test.hpp"
#include "desktop/windows/video/shaders/processor.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <windows.h>

using namespace cupid::desktop;

namespace {

struct Directory {
  std::filesystem::path path;
  Directory() {
    auto number = std::chrono::steady_clock::now().time_since_epoch().count();
    do {
      path = std::filesystem::temp_directory_path() /
             (L"cupid-shader-\u96ea-" + std::to_wstring(number++));
    } while (!std::filesystem::create_directory(path));
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

cupid::n64::VideoFrame image(unsigned width = 31, unsigned height = 19) {
  cupid::n64::VideoFrame frame{width, height,
                               std::vector<std::uint8_t>(std::size_t(width) * height * 4)};
  for (std::size_t index = 0; index < frame.rgba.size(); index += 4) {
    frame.rgba[index] = static_cast<std::uint8_t>((index * 13) & 255);
    frame.rgba[index + 1] = static_cast<std::uint8_t>((index * 29 + 31) & 255);
    frame.rgba[index + 2] = static_cast<std::uint8_t>((index * 53 + 97) & 255);
    frame.rgba[index + 3] = 255;
  }
  return frame;
}

void effects() {
  ShaderProcessor shader;
  const auto frame = image();
  const auto before = frame.rgba;
  test::equal(shader.apply(frame).data() == frame.rgba.data(), true);
  for (const auto preset : {DisplayShader::Soft, DisplayShader::Scanlines}) {
    shader.select({preset, {}});
    const auto filtered = shader.apply(frame);
    test::equal(shader.error().empty(), true);
    if (!shader.error().empty())
      std::cerr << shader.error() << '\n';
    test::equal(filtered.size(), frame.rgba.size());
    test::equal(std::equal(filtered.begin(), filtered.end(), before.begin()), false);
    test::equal(frame.rgba == before, true);
    for (std::size_t index = 3; index < filtered.size(); index += 4)
      test::equal(filtered[index], 255);
  }
  shader.select({});
  const auto removed = shader.apply(frame);
  test::equal(removed.data() == frame.rgba.data(), true);
  test::equal(shader.error().empty(), true);
}

void files(const Directory &directory) {
  const auto path = directory.path / L"invert-\u96ea.hlsl";
  const auto write = [&](const char *source) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << source;
    if (!stream)
      throw std::runtime_error("Could not write the shader fixture.");
  };
  write(R"(
Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);
cbuffer Dimensions : register(b0) { float4 SourceSize; };
float4 main(float4 p : SV_Position) : SV_Target {
  float4 color = SourceTexture.SampleLevel(SourceSampler, p.xy * SourceSize.zw, 0);
  return float4(1 - color.rgb, color.a);
})");
  ShaderProcessor shader;
  const ShaderSelection selection{DisplayShader::File, path};
  shader.select(selection);
  for (const auto size : {std::pair{31u, 19u}, std::pair{7u, 3u}, std::pair{63u, 17u}}) {
    const auto frame = image(size.first, size.second);
    const auto original = frame.rgba;
    const auto filtered = shader.apply(frame);
    test::equal(shader.error().empty(), true);
    if (!shader.error().empty())
      std::cerr << shader.error() << '\n';
    test::equal(filtered.size(), original.size());
    for (std::size_t index = 0; index < original.size(); ++index) {
      const unsigned expected = index % 4 == 3 ? 255 : 255 - original[index];
      test::equal(filtered[index], expected);
    }
    test::equal(frame.rgba == original, true);
  }
  const auto frame = image();
  write("this is not a pixel shader");
  shader.select(selection);
  test::equal(shader.apply(frame).data() == frame.rgba.data(), true);
  test::equal(shader.error().find("compilation failed") != std::string::npos, true);
  shader.select({DisplayShader::File, directory.path / L"missing.hlsl"});
  test::equal(shader.apply(frame).data() == frame.rgba.data(), true);
  test::equal(shader.error().find("missing") != std::string::npos, true);
  shader.select({DisplayShader::Scanlines, {}});
  test::equal(shader.apply(frame).data() != frame.rgba.data(), true);
  test::equal(shader.error().empty(), true);
  auto invalid = frame;
  invalid.width = 8193;
  test::equal(shader.apply(invalid).data() == invalid.rgba.data(), true);
  test::equal(!shader.error().empty(), true);
  shader.select({});
  test::equal(shader.apply(frame).data() == frame.rgba.data(), true);
}

void persistence(const Directory &directory) {
  const auto settings = directory.path / L"settings.ini";
  const ShaderSelection file{DisplayShader::File, directory.path / L"missing-\u96ea.hlsl"};
  for (const auto selected : {ShaderSelection{}, ShaderSelection{DisplayShader::Soft, {}},
                              ShaderSelection{DisplayShader::Scanlines, {}}, file}) {
    save_shader_selection(settings, selected);
    test::equal(read_shader_selection(settings) == selected, true);
  }
  WritePrivateProfileStringW(L"Shader", L"Preset", L"99", settings.c_str());
  test::equal(read_shader_selection(settings) == ShaderSelection{}, true);
  bool rejected = false;
  try {
    save_shader_selection(settings, {DisplayShader::File, {}});
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  test::equal(rejected, true);
  rejected = false;
  try {
    save_shader_selection(directory.path, file);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  test::equal(rejected, true);
}

} // namespace

int main() {
  Directory directory;
  effects();
  files(directory);
  persistence(directory);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
