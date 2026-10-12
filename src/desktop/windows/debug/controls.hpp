#pragma once

#include "desktop/windows/dialogs/form.hpp"
#include "desktop/windows/dialogs/scroll.hpp"
#include <stdexcept>

namespace cupid::desktop::inspection_controls {

inline std::wstring wide(std::string_view text) {
  return {text.begin(), text.end()};
}

inline HWND field(HWND dialog, const ScrollForm &scroll, unsigned id) {
  const auto direct = GetDlgItem(dialog, id);
  return direct ? direct : GetDlgItem(scroll.content(), id);
}

inline void text(HWND dialog, const ScrollForm &scroll, unsigned id, std::string_view value) {
  SetWindowTextW(field(dialog, scroll, id), wide(value).c_str());
}

inline std::string text(HWND dialog, const ScrollForm &scroll, unsigned id) {
  const auto edit = field(dialog, scroll, id);
  const auto length = GetWindowTextLengthW(edit);
  if (length < 0 || length > 16384)
    throw std::runtime_error("The entered value is too long.");
  std::wstring value(std::size_t(length) + 1, L'\0');
  value.resize(GetWindowTextW(edit, value.data(), length + 1));
  if (std::any_of(value.begin(), value.end(), [](wchar_t ch) { return ch > 127; }))
    throw std::runtime_error("Use hexadecimal digits and spaces.");
  std::string result(value.size(), '\0');
  std::transform(value.begin(), value.end(), result.begin(),
                 [](wchar_t ch) { return static_cast<char>(ch); });
  return result;
}

inline void monospace(HWND edit) {
  SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(SYSTEM_FIXED_FONT)), TRUE);
  SendMessageW(edit, EM_SETLIMITTEXT, 65536, 0);
}

} // namespace cupid::desktop::inspection_controls
