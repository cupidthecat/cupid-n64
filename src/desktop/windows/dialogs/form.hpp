#pragma once

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>
#include <windows.h>

namespace cupid::desktop {

inline std::vector<WORD> dialog_template(const wchar_t *title, WORD width, WORD height) {
  std::vector<WORD> data;
  const auto word = [&](WORD value) { data.push_back(value); };
  const auto dword = [&](DWORD value) {
    word(static_cast<WORD>(value));
    word(static_cast<WORD>(value >> 16));
  };
  const auto text = [&](const wchar_t *value) {
    do {
      word(static_cast<WORD>(*value));
    } while (*value++);
  };
  dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT);
  dword(0);
  for (WORD value : std::array<WORD, 7>{0, 0, 0, width, height, 0, 0})
    word(value);
  text(title);
  word(9);
  text(L"Segoe UI");
  return data;
}

inline HWND dialog_control(HWND dialog, const wchar_t *type, const wchar_t *text, DWORD style,
                           unsigned id, int x, int y, int width, int height) {
  RECT bounds{x, y, x + width, y + height};
  MapDialogRect(dialog, &bounds);
  const auto child =
      CreateWindowExW(type == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text,
                      WS_CHILD | WS_VISIBLE | style, bounds.left, bounds.top,
                      bounds.right - bounds.left, bounds.bottom - bounds.top, dialog,
                      reinterpret_cast<HMENU>(UINT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
  return child;
}

inline void center_dialog(HWND dialog) {
  RECT bounds{}, parent{};
  GetWindowRect(dialog, &bounds);
  GetWindowRect(GetParent(dialog), &parent);
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
  const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  const int x = std::clamp(int(parent.left + (parent.right - parent.left - width) / 2),
                           int(monitor.rcWork.left),
                           std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - width));
  const int y = std::clamp(int(parent.top + (parent.bottom - parent.top - height) / 2),
                           int(monitor.rcWork.top),
                           std::max(int(monitor.rcWork.top), int(monitor.rcWork.bottom) - height));
  SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

} // namespace cupid::desktop
