#include "desktop/windows/controls/keys.hpp"
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace cupid::desktop {

std::wstring key_name(unsigned code) {
  switch (code) {
  case VK_SHIFT:
    return L"Shift (either)";
  case VK_CONTROL:
    return L"Ctrl (either)";
  case VK_MENU:
    return L"Alt (either)";
  case VK_LSHIFT:
    return L"Left Shift";
  case VK_RSHIFT:
    return L"Right Shift";
  case VK_LCONTROL:
    return L"Left Ctrl";
  case VK_RCONTROL:
    return L"Right Ctrl";
  case VK_LMENU:
    return L"Left Alt";
  case VK_RMENU:
    return L"Right Alt";
  }
  const auto scan = MapVirtualKeyW(code, MAPVK_VK_TO_VSC_EX);
  const auto parameter = static_cast<LONG>((scan & 255) << 16) | (scan & 0xff00 ? 1L << 24 : 0);
  std::array<wchar_t, 128> name{};
  if (GetKeyNameTextW(parameter, name.data(), static_cast<int>(name.size())))
    return name.data();
  std::wostringstream text;
  text << L"Key " << std::hex << std::setw(2) << std::setfill(L'0') << code;
  return text.str();
}

std::wstring shortcut_text(ShortcutBinding binding) {
  if (!binding.key)
    return {};
  std::wstring text;
  if (binding.modifiers & ShortcutCtrl)
    text += L"Ctrl+";
  if (binding.modifiers & ShortcutShift)
    text += L"Shift+";
  if (binding.modifiers & ShortcutAlt)
    text += L"Alt+";
  return text + key_name(binding.key);
}

std::wstring shortcut_text(const std::array<ShortcutBinding, 2> &bindings) {
  auto text = shortcut_text(bindings[0]);
  const auto alternative = shortcut_text(bindings[1]);
  if (!alternative.empty()) {
    if (!text.empty())
      text += L" / ";
    text += alternative;
  }
  return text;
}

} // namespace cupid::desktop
