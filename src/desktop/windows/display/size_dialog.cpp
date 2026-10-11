#include "desktop/windows/display/size_dialog.hpp"
#include <algorithm>
#include <array>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cupid::desktop {
namespace {

struct DialogState {
  ViewportSize size;
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  if (event == WM_INITDIALOG) {
    auto *state = reinterpret_cast<DialogState *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    const auto control = [&](const wchar_t *type, const wchar_t *text, DWORD style, unsigned id,
                             int x, int y, int width, int height) {
      RECT bounds{x, y, x + width, y + height};
      MapDialogRect(dialog, &bounds);
      const auto child = CreateWindowExW(
          type == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text,
          WS_CHILD | WS_VISIBLE | style, bounds.left, bounds.top, bounds.right - bounds.left,
          bounds.bottom - bounds.top, dialog, reinterpret_cast<HMENU>(UINT_PTR(id)),
          GetModuleHandleW(nullptr), nullptr);
      SendMessageW(child, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    };
    control(L"STATIC", L"Viewport size in logical pixels; scales with display DPI.", 0, 0, 10, 10,
            210, 20);
    control(L"STATIC", L"&Width:", 0, 0, 10, 38, 45, 12);
    control(L"EDIT", std::to_wstring(state->size.width).c_str(), ES_NUMBER | WS_TABSTOP, 100, 60,
            35, 65, 16);
    control(L"STATIC", L"&Height:", 0, 0, 10, 61, 45, 12);
    control(L"EDIT", std::to_wstring(state->size.height).c_str(), ES_NUMBER | WS_TABSTOP, 101, 60,
            58, 65, 16);
    control(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 112, 84, 50, 14);
    control(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 168, 84, 50, 14);
    RECT bounds{}, parent{};
    GetWindowRect(dialog, &bounds);
    GetWindowRect(GetParent(dialog), &parent);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const int x = std::clamp(int(parent.left + (parent.right - parent.left - width) / 2),
                             int(monitor.rcWork.left),
                             std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - width));
    const int y = std::clamp(
        int(parent.top + (parent.bottom - parent.top - height) / 2), int(monitor.rcWork.top),
        std::max(int(monitor.rcWork.top), int(monitor.rcWork.bottom) - height));
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    return TRUE;
  }
  if (event != WM_COMMAND)
    return FALSE;
  if (LOWORD(wparam) == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  if (LOWORD(wparam) != IDOK)
    return FALSE;
  ViewportSize size;
  const auto read = [&](unsigned id) {
    std::array<wchar_t, 16> text{};
    const auto length =
        GetDlgItemTextW(dialog, static_cast<int>(id), text.data(), static_cast<int>(text.size()));
    if (!length || length >= static_cast<int>(text.size() - 1) ||
        !std::all_of(text.begin(), text.begin() + length,
                     [](wchar_t letter) { return letter >= L'0' && letter <= L'9'; }))
      return 0u;
    BOOL valid = FALSE;
    const auto value = GetDlgItemInt(dialog, static_cast<int>(id), &valid, FALSE);
    return valid ? value : 0u;
  };
  size.width = read(100);
  size.height = read(101);
  if (size.width < 64 || size.width > 8192 || size.height < 64 || size.height > 8192) {
    MessageBoxW(dialog, L"Enter a width and height from 64 through 8192.", L"Viewport size",
                MB_OK | MB_ICONERROR);
    return TRUE;
  }
  auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dialog, DWLP_USER));
  state->size = size;
  EndDialog(dialog, IDOK);
  return TRUE;
}

} // namespace

std::optional<ViewportSize> choose_viewport_size(HWND owner, ViewportSize current) {
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
  for (WORD value : std::array<WORD, 7>{0, 0, 0, 230, 108, 0, 0})
    word(value);
  text(L"Viewport size");
  word(9);
  text(L"Segoe UI");
  DialogState state{current};
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&state));
  if (result == -1)
    throw std::runtime_error("Could not open the viewport-size dialog.");
  return result == IDOK ? std::optional(state.size) : std::nullopt;
}

} // namespace cupid::desktop
