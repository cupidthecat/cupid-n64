#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include <array>
#include <stdexcept>

namespace cupid::desktop {
namespace {

constexpr unsigned choice_id = 90, edit_first = 100, browse_first = 200;

void read(HWND dialog, PathForm &form) {
  for (unsigned n = 0; n < form.fields.size(); ++n) {
    std::array<wchar_t, 32768> path{};
    const auto length =
        GetDlgItemTextW(dialog, edit_first + n, path.data(), static_cast<int>(path.size()));
    if (length >= static_cast<int>(path.size() - 1))
      throw std::runtime_error("The selected path is too long.");
    form.fields[n].path = path.data();
  }
}

void refresh(HWND dialog, const PathForm &form) {
  SetDlgItemTextW(dialog, 80, form.explanation.c_str());
  for (unsigned n = 0; n < form.fields.size(); ++n) {
    SetDlgItemTextW(dialog, 300 + n, form.fields[n].label.c_str());
    SetDlgItemTextW(dialog, edit_first + n, form.fields[n].path.c_str());
    EnableWindow(GetDlgItem(dialog, edit_first + n), form.fields[n].enabled);
    EnableWindow(GetDlgItem(dialog, browse_first + n), form.fields[n].enabled);
  }
}

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *form = reinterpret_cast<PathForm *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    form = reinterpret_cast<PathForm *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    int y = 10;
    if (!form->choices.empty()) {
      dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                     choice_id, 10, y, 360, 180);
      const auto choices = GetDlgItem(dialog, choice_id);
      for (const auto &label : form->choices)
        SendMessageW(choices, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
      SendMessageW(choices, CB_SETCURSEL, form->selected, 0);
      y += 25;
    }
    dialog_control(dialog, L"STATIC", L"", 0, 80, 10, y, 430, 45);
    y += 50;
    for (unsigned n = 0; n < form->fields.size(); ++n) {
      dialog_control(dialog, L"STATIC", L"", 0, 300 + n, 10, y, 430, 12);
      dialog_control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, edit_first + n, 10, y + 14,
                     370, 16);
      dialog_control(dialog, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, browse_first + n,
                     386, y + 14, 54, 16);
      y += 38;
    }
    dialog_control(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 330, y + 4, 50,
                   16);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 390, y + 4,
                   50, 16);
    refresh(dialog, *form);
    center_dialog(dialog);
    return TRUE;
  }
  if (event != WM_COMMAND || !form)
    return FALSE;
  const auto id = LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  try {
    if (id == choice_id && HIWORD(wparam) == CBN_SELCHANGE) {
      read(dialog, *form);
      form->selected =
          static_cast<unsigned>(SendDlgItemMessageW(dialog, choice_id, CB_GETCURSEL, 0, 0));
      if (form->changed)
        form->changed(*form);
      refresh(dialog, *form);
      return TRUE;
    }
    if (id >= browse_first && id < browse_first + form->fields.size()) {
      read(dialog, *form);
      auto &field = form->fields[id - browse_first];
      auto directory =
          field.path.empty() || field.kind == FileSelection::Pif || field.kind == FileSelection::Ipl
              ? form->directory
              : (field.kind == FileSelection::Folder ? field.path : field.path.parent_path());
      if (!std::filesystem::is_directory(directory))
        directory = std::filesystem::is_directory(form->directory) ? form->directory
                                                                   : std::filesystem::path{};
      if (const auto path =
              select_file(dialog, field.label.c_str(), field.kind, directory, field.path))
        field.path = *path;
      refresh(dialog, *form);
      return TRUE;
    }
    if (id != IDOK)
      return FALSE;
    read(dialog, *form);
    if (form->validate)
      form->validate(*form);
    EndDialog(dialog, IDOK);
  } catch (const std::exception &error) {
    MessageBoxA(dialog, error.what(), "Cupid-N64 settings", MB_OK | MB_ICONERROR);
  }
  return TRUE;
}

} // namespace

std::optional<PathForm> choose_paths(HWND owner, PathForm form) {
  const auto height =
      static_cast<WORD>(95 + form.fields.size() * 38 + (form.choices.empty() ? 0 : 25));
  const auto data = dialog_template(form.title.c_str(), 450, height);
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&form));
  if (result == -1)
    throw std::runtime_error("Could not open the settings dialog.");
  return result == IDOK ? std::optional(std::move(form)) : std::nullopt;
}

} // namespace cupid::desktop
