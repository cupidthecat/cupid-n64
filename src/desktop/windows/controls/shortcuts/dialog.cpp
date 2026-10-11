#include "desktop/windows/controls/shortcuts/dialog.hpp"
#include "desktop/windows/controls/keys.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include "desktop/windows/dialogs/scroll.hpp"
#include <stdexcept>

namespace cupid::desktop {
namespace {

constexpr unsigned defaults_id = 90, clear_id = 91, actions_id = 100, selected_id = 101,
                   key_first = 200, modifier_first = 210, conflict_id = 220;

struct DialogState {
  ShortcutBindings bindings;
  unsigned selected = 0;
  ScrollForm layout;
};

void read(HWND content, DialogState &state) {
  for (unsigned slot = 0; slot < 2; ++slot) {
    auto &binding = state.bindings[state.selected][slot];
    const auto choice = SendDlgItemMessageW(content, key_first + slot, CB_GETCURSEL, 0, 0);
    binding.key = choice == CB_ERR ? 0
                                   : static_cast<unsigned>(SendDlgItemMessageW(
                                         content, key_first + slot, CB_GETITEMDATA, choice, 0));
    binding.modifiers = !binding.key ? 0
                                     : static_cast<unsigned>(SendDlgItemMessageW(
                                           content, modifier_first + slot, CB_GETCURSEL, 0, 0));
  }
}

void assignments(HWND content, const DialogState &state) {
  const auto list = GetDlgItem(content, actions_id);
  const auto top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (unsigned action = 0; action < state.bindings.size(); ++action) {
    const auto binding = shortcut_text(state.bindings[action]);
    const auto text = std::wstring(shortcut_name(static_cast<ShortcutAction>(action))) + L": " +
                      (binding.empty() ? L"Unassigned" : binding);
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
  }
  SendMessageW(list, LB_SETCURSEL, state.selected, 0);
  SendMessageW(list, LB_SETTOPINDEX, top < 0 ? 0 : top, 0);
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  std::wstring status = L"No shortcut conflicts.";
  if (const auto conflict = shortcut_conflict(state.bindings))
    status = shortcut_text(conflict->binding) + L" is assigned to " +
             std::wstring(shortcut_name(conflict->first)) + L" and " +
             std::wstring(shortcut_name(conflict->second)) + L". Clear or change one assignment.";
  SetDlgItemTextW(content, conflict_id, status.c_str());
}

void refresh(HWND content, const DialogState &state) {
  SetDlgItemTextW(content, selected_id,
                  shortcut_name(static_cast<ShortcutAction>(state.selected)).data());
  for (unsigned slot = 0; slot < 2; ++slot) {
    const auto binding = state.bindings[state.selected][slot];
    const auto key = GetDlgItem(content, key_first + slot);
    const auto count = SendMessageW(key, CB_GETCOUNT, 0, 0);
    for (LRESULT choice = 0; choice < count; ++choice)
      if (SendMessageW(key, CB_GETITEMDATA, choice, 0) == binding.key) {
        SendMessageW(key, CB_SETCURSEL, choice, 0);
        break;
      }
    SendDlgItemMessageW(content, modifier_first + slot, CB_SETCURSEL, binding.modifiers, 0);
  }
  assignments(content, state);
}

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    state = reinterpret_cast<DialogState *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"STATIC",
                   L"Choose an action to edit either assignment. Guest controller mappings are "
                   L"configured separately in Input mappings.",
                   0, 0, 10, 10, 500, 25);
    dialog_control(dialog, L"LISTBOX", L"", LBS_NOTIFY | WS_TABSTOP | WS_VSCROLL | WS_BORDER,
                   actions_id, 10, 40, 500, 190);
    dialog_control(dialog, L"STATIC", L"", 0, selected_id, 10, 236, 500, 14);
    constexpr DWORD dropdown = CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL;
    for (unsigned slot = 0; slot < 2; ++slot) {
      const int y = 259 + int(slot) * 25;
      dialog_control(dialog, L"STATIC", slot ? L"Alternative:" : L"Shortcut:", 0, 0, 10, y + 3, 80,
                     12);
      const auto keys =
          dialog_control(dialog, L"COMBOBOX", L"", dropdown, key_first + slot, 95, y, 230, 180);
      SendMessageW(keys, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None"));
      SendMessageW(keys, CB_SETITEMDATA, 0, 0);
      for (unsigned key = 8; key < 256; ++key)
        if (valid_shortcut({key, 0})) {
          const auto name = key_name(key);
          const auto index =
              SendMessageW(keys, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
          SendMessageW(keys, CB_SETITEMDATA, index, key);
        }
      const auto modifiers = dialog_control(dialog, L"COMBOBOX", L"", dropdown,
                                            modifier_first + slot, 335, y, 175, 140);
      for (const auto name : {L"No modifiers", L"Ctrl", L"Shift", L"Ctrl+Shift", L"Alt",
                              L"Ctrl+Alt", L"Shift+Alt", L"Ctrl+Shift+Alt"})
        SendMessageW(modifiers, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    dialog_control(dialog, L"STATIC", L"", 0, conflict_id, 10, 315, 500, 32);
    dialog_control(dialog, L"BUTTON", L"Restore defaults", BS_PUSHBUTTON | WS_TABSTOP, defaults_id,
                   10, 363, 100, 16);
    dialog_control(dialog, L"BUTTON", L"Clear selected", BS_PUSHBUTTON | WS_TABSTOP, clear_id, 120,
                   363, 100, 16);
    dialog_control(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 400, 363, 50, 16);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 460, 363, 50,
                   16);
    refresh(dialog, *state);
    try {
      state->layout.initialize(dialog, 353);
    } catch (const std::exception &error) {
      MessageBoxA(dialog, error.what(), "Shortcuts", MB_OK | MB_ICONERROR);
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    center_dialog(dialog);
    return TRUE;
  }
  if (!state)
    return FALSE;
  if (state->layout.message(event, wparam))
    return TRUE;
  if (event != WM_COMMAND)
    return FALSE;
  const auto content = state->layout.content();
  const auto id = LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  try {
    if (id == IDOK) {
      read(content, *state);
      if (const auto conflict = shortcut_conflict(state->bindings)) {
        const auto text = shortcut_text(conflict->binding) + L" is assigned to " +
                          std::wstring(shortcut_name(conflict->first)) + L" and " +
                          std::wstring(shortcut_name(conflict->second)) + L".";
        MessageBoxW(dialog, text.c_str(), L"Shortcut conflict", MB_OK | MB_ICONERROR);
      } else {
        validate_shortcuts(state->bindings);
        EndDialog(dialog, IDOK);
      }
    } else if (id == actions_id && HIWORD(wparam) == LBN_SELCHANGE) {
      read(content, *state);
      const auto selected = SendDlgItemMessageW(content, actions_id, LB_GETCURSEL, 0, 0);
      if (selected >= 0 && selected < shortcut_count)
        state->selected = static_cast<unsigned>(selected);
      refresh(content, *state);
    } else if (id == defaults_id) {
      state->bindings = default_shortcuts();
      refresh(content, *state);
    } else if (id == clear_id) {
      state->bindings[state->selected] = {};
      refresh(content, *state);
    } else if ((id >= key_first && id < key_first + 2) ||
               (id >= modifier_first && id < modifier_first + 2)) {
      if (HIWORD(wparam) == CBN_SELCHANGE) {
        read(content, *state);
        assignments(content, *state);
      }
    }
  } catch (const std::exception &error) {
    MessageBoxA(dialog, error.what(), "Shortcuts", MB_OK | MB_ICONERROR);
  }
  return TRUE;
}

} // namespace

std::optional<ShortcutBindings> choose_shortcuts(HWND owner, ShortcutBindings bindings) {
  DialogState state{std::move(bindings), 0, {}};
  const auto form = dialog_template(L"Desktop shortcuts", 520, 391);
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(form.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&state));
  if (result == -1)
    throw std::runtime_error("Could not open shortcut settings.");
  return result == IDOK ? std::optional(std::move(state.bindings)) : std::nullopt;
}

} // namespace cupid::desktop
