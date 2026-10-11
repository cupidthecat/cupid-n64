#include "desktop/windows/commands.hpp"
#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {
namespace {

std::wstring diagnostic_text(const std::string &text) {
  const int bytes = static_cast<int>(text.size());
  UINT encoding = CP_UTF8;
  int count = MultiByteToWideChar(encoding, MB_ERR_INVALID_CHARS, text.data(), bytes, nullptr, 0);
  if (!count) {
    encoding = CP_ACP;
    count = MultiByteToWideChar(encoding, 0, text.data(), bytes, nullptr, 0);
  }
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (count)
    MultiByteToWideChar(encoding, 0, text.data(), bytes, result.data(), count);
  return result;
}

} // namespace

void Window::create_shader_menu(HMENU view) {
  shader_menu_ = CreatePopupMenu();
  AppendMenuW(shader_menu_, MF_STRING, ShaderNone, L"&None");
  AppendMenuW(shader_menu_, MF_STRING, ShaderSoft, L"&Soft");
  AppendMenuW(shader_menu_, MF_STRING, ShaderScanlines, L"&Scanlines");
  AppendMenuW(shader_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(shader_menu_, MF_STRING, ShaderFile, L"Choose &shader file (.hlsl)...");
  AppendMenuW(shader_menu_, MF_STRING, ShaderReload, L"&Reload selected shader");
  AppendMenuW(shader_menu_, MF_STRING, ShaderDetails, L"&Error details...");
  AppendMenuW(shader_menu_, MF_STRING, ShaderFormat, L"Shader file &format...");
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(shader_menu_), L"Display s&hader");
  update_shader();
}

void Window::update_shader() {
  shader_message_ = shader_.error().empty() ? L""
                                            : L"Display shader unavailable; unfiltered image "
                                              L"shown. View > Display shader > Error details";
  if (!shader_menu_)
    return;
  const auto &selected = shader_.selection();
  const auto label = selected.file.empty() ? std::wstring(L"Choose &shader file (.hlsl)...")
                                           : L"Choose &shader file (.hlsl)... [" +
                                                 selected.file.filename().wstring() + L"]";
  ModifyMenuW(shader_menu_, ShaderFile, MF_BYCOMMAND | MF_STRING, ShaderFile, label.c_str());
  CheckMenuRadioItem(shader_menu_, ShaderNone, ShaderFile,
                     ShaderNone + static_cast<unsigned>(selected.preset), MF_BYCOMMAND);
  EnableMenuItem(shader_menu_, ShaderReload,
                 MF_BYCOMMAND | (selected.preset != DisplayShader::None ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(shader_menu_, ShaderDetails,
                 MF_BYCOMMAND | (!shader_.error().empty() ? MF_ENABLED : MF_GRAYED));
  if (window_)
    DrawMenuBar(window_);
}

void Window::select_shader(ShaderSelection selected) {
  if (!options_.frames)
    save_shader_selection(settings_, selected);
  shader_.select(std::move(selected));
  if (session_)
    refresh_frame();
  update_shader();
}

bool Window::shader_command(unsigned id) {
  if (id < ShaderNone || id > ShaderFormat)
    return false;
  try {
    auto selected = shader_.selection();
    if (id >= ShaderNone && id <= ShaderScanlines) {
      selected.preset = static_cast<DisplayShader>(id - ShaderNone);
      select_shader(std::move(selected));
    } else if (id == ShaderFile) {
      std::error_code error;
      auto directory = selected.file.parent_path();
      if (directory.empty() || !std::filesystem::is_directory(directory, error))
        directory = application_directory();
      const auto file = select_file(window_, L"Choose an HLSL display pixel shader",
                                    FileSelection::Shader, directory, selected.file);
      if (file) {
        selected = {DisplayShader::File, *file};
        select_shader(std::move(selected));
      }
    } else if (id == ShaderReload) {
      select_shader(std::move(selected));
    } else if (id == ShaderDetails && !shader_.error().empty()) {
      const auto details = diagnostic_text(shader_.error());
      MessageBoxW(window_, details.c_str(), L"Display shader error", MB_OK | MB_ICONINFORMATION);
    } else if (id == ShaderFormat) {
      MessageBoxW(
          window_,
          L"Choose a single HLSL pixel shader file with a main entry point (shader model 4.0).\n"
          L"SourceSize contains width, height, 1/width and 1/height.\n\n"
          L"Texture2D<float4> SourceTexture : register(t0);\n"
          L"SamplerState SourceSampler : register(s0);\n"
          L"cbuffer Dimensions : register(b0) { float4 SourceSize; };\n"
          L"float4 main(float4 p : SV_Position) : SV_Target {\n"
          L"  return SourceTexture.Load(int3(int2(p.xy), 0));\n}"
          L"\n\nEffects change the displayed image and screenshots. Select None to remove the "
          L"effect.",
          L"Display shader file format", MB_OK | MB_ICONINFORMATION);
    }
  } catch (const std::exception &error) {
    status_error(L"Display shader selection failed", error);
  }
  rebase();
  return true;
}

} // namespace cupid::desktop
