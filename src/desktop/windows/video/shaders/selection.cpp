#include "desktop/windows/video/shaders/selection.hpp"
#include "desktop/windows/settings/encoding.hpp"
#include <array>
#include <stdexcept>
#include <windows.h>

namespace cupid::desktop {

ShaderSelection read_shader_selection(const std::filesystem::path &settings) {
  if (settings.empty())
    return {};
  ShaderSelection selection;
  selection.preset =
      static_cast<DisplayShader>(GetPrivateProfileIntW(L"Shader", L"Preset", 0, settings.c_str()));
  std::array<wchar_t, 32768> path{};
  const auto length = GetPrivateProfileStringW(L"Shader", L"File", L"", path.data(),
                                               static_cast<DWORD>(path.size()), settings.c_str());
  if (length < path.size() - 1)
    selection.file = path.data();
  return selection.valid() ? selection : ShaderSelection{};
}

void save_shader_selection(const std::filesystem::path &settings,
                           const ShaderSelection &selection) {
  if (!selection.valid())
    throw std::runtime_error("Select a supported display shader.");
  if (settings.empty())
    return;
  unicode_settings(settings);
  const std::wstring section = L"Preset=" +
                               std::to_wstring(static_cast<unsigned>(selection.preset)) + L'\0' +
                               L"File=" + selection.file.wstring() + L'\0';
  if (!WritePrivateProfileSectionW(L"Shader", section.c_str(), settings.c_str()))
    throw std::runtime_error("Could not save the display shader. Check folder permissions.");
}

} // namespace cupid::desktop
