#include "desktop/windows/dialogs/paths.hpp"
#include <algorithm>
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
    int y = 10;
    if (!form->choices.empty()) {
      control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, choice_id, 10, y, 360,
              180);
      const auto choices = GetDlgItem(dialog, choice_id);
      for (const auto &label : form->choices)
        SendMessageW(choices, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
      SendMessageW(choices, CB_SETCURSEL, form->selected, 0);
      y += 25;
    }
    control(L"STATIC", L"", 0, 80, 10, y, 430, 45);
    y += 50;
    for (unsigned n = 0; n < form->fields.size(); ++n) {
      control(L"STATIC", L"", 0, 300 + n, 10, y, 430, 12);
      control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, edit_first + n, 10, y + 14, 370, 16);
      control(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, browse_first + n, 386, y + 14,
              54, 16);
      y += 38;
    }
    control(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 330, y + 4, 50, 16);
    control(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 390, y + 4, 50, 16);
    refresh(dialog, *form);
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
  const auto height =
      static_cast<WORD>(95 + form.fields.size() * 38 + (form.choices.empty() ? 0 : 25));
  for (WORD value : std::array<WORD, 7>{0, 0, 0, 450, height, 0, 0})
    word(value);
  text(form.title.c_str());
  word(9);
  text(L"Segoe UI");
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&form));
  if (result == -1)
    throw std::runtime_error("Could not open the settings dialog.");
  return result == IDOK ? std::optional(std::move(form)) : std::nullopt;
}

} // namespace cupid::desktop
