#include "desktop/windows/display/size_dialog.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace cupid::desktop {
namespace {

struct DialogState {
  ViewportSize size;
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  if (event == WM_INITDIALOG) {
    auto *state = reinterpret_cast<DialogState *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"STATIC", L"Viewport size in logical pixels; scales with display DPI.",
                   0, 0, 10, 10, 210, 20);
    dialog_control(dialog, L"STATIC", L"&Width:", 0, 0, 10, 38, 45, 12);
    dialog_control(dialog, L"EDIT", std::to_wstring(state->size.width).c_str(),
                   ES_NUMBER | WS_TABSTOP, 100, 60, 35, 65, 16);
    dialog_control(dialog, L"STATIC", L"&Height:", 0, 0, 10, 61, 45, 12);
    dialog_control(dialog, L"EDIT", std::to_wstring(state->size.height).c_str(),
                   ES_NUMBER | WS_TABSTOP, 101, 60, 58, 65, 16);
    dialog_control(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 112, 84, 50, 14);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 168, 84, 50,
                   14);
    center_dialog(dialog);
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
  const auto data = dialog_template(L"Viewport size", 230, 108);
  DialogState state{current};
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&state));
  if (result == -1)
    throw std::runtime_error("Could not open the viewport-size dialog.");
  return result == IDOK ? std::optional(state.size) : std::nullopt;
}

} // namespace cupid::desktop
