#include "desktop/windows/settings/transfer/review.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/dialogs/scroll.hpp"
#include <array>
#include <stdexcept>

namespace cupid::desktop {
namespace {

constexpr unsigned list_id = 100, path_id = 101, browse_id = 102, apply_id = 103;

struct Review {
  SettingsTransfer settings;
  std::vector<settings::LocalPath> paths;
  unsigned selected = 0;
  ScrollForm scroll;
};

HWND content(HWND dialog, const Review &review) {
  return review.scroll.content() ? review.scroll.content() : dialog;
}

FileSelection selection(settings::PathKind kind) {
  constexpr std::array kinds{
      FileSelection::Cartridge, FileSelection::Pif,      FileSelection::Ipl,   FileSelection::Disk,
      FileSelection::Folder,    FileSelection::Handheld, FileSelection::Shader};
  return kinds.at(static_cast<unsigned>(kind));
}

void refresh(HWND dialog, Review &review) {
  const auto parent = content(dialog, review);
  const auto list = GetDlgItem(parent, list_id);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (const auto &field : review.paths) {
    const auto label = field.section + L" / " + field.key;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
  }
  SendMessageW(list, LB_SETCURSEL, review.selected, 0);
  SetDlgItemTextW(parent, path_id, review.paths[review.selected].path.c_str());
}

void edit(HWND dialog, Review &review) {
  std::array<wchar_t, 32768> path{};
  const auto length =
      GetDlgItemTextW(content(dialog, review), path_id, path.data(), static_cast<int>(path.size()));
  if (length >= static_cast<int>(path.size() - 1))
    throw std::runtime_error("The edited settings path is too long.");
  auto &field = review.paths[review.selected];
  review.settings.path(field, path.data());
  field.path = path.data();
}

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *review = reinterpret_cast<Review *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    review = reinterpret_cast<Review *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"STATIC",
                   L"These imported files or folders are unavailable on this computer. Select a "
                   L"path to edit or browse. You can keep missing paths and fix them later.",
                   0, 90, 10, 10, 430, 38);
    dialog_control(dialog, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, list_id, 10, 52,
                   430, 100);
    dialog_control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, path_id, 10, 158, 430, 18);
    dialog_control(dialog, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, browse_id, 10, 182,
                   64, 18);
    dialog_control(dialog, L"BUTTON", L"Apply path edit", BS_PUSHBUTTON | WS_TABSTOP, apply_id, 80,
                   182, 90, 18);
    dialog_control(dialog, L"BUTTON", L"Import", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 322, 216, 56,
                   18);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 384, 216, 56,
                   18);
    refresh(dialog, *review);
    review->scroll.initialize(dialog, 212);
    center_dialog(dialog);
    return TRUE;
  }
  if (review && review->scroll.message(event, wparam))
    return TRUE;
  if (!review || event != WM_COMMAND)
    return FALSE;
  const auto id = LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  try {
    if (id == list_id && HIWORD(wparam) == LBN_SELCHANGE) {
      edit(dialog, *review);
      const auto selected =
          SendDlgItemMessageW(content(dialog, *review), list_id, LB_GETCURSEL, 0, 0);
      if (selected != LB_ERR)
        review->selected = static_cast<unsigned>(selected);
      refresh(dialog, *review);
    } else if (id == browse_id) {
      edit(dialog, *review);
      auto &field = review->paths[review->selected];
      std::error_code error;
      const auto parent =
          field.kind == settings::PathKind::Folder ? field.path : field.path.parent_path();
      const auto directory =
          std::filesystem::is_directory(parent, error) ? parent : std::filesystem::path{};
      if (const auto path = select_file(dialog, L"Choose a replacement settings path",
                                        selection(field.kind), directory, field.path)) {
        review->settings.path(field, *path);
        field.path = *path;
      }
      refresh(dialog, *review);
    } else if (id == apply_id || id == IDOK) {
      edit(dialog, *review);
      if (id == IDOK)
        EndDialog(dialog, IDOK);
    } else {
      return FALSE;
    }
  } catch (const std::exception &error) {
    MessageBoxA(dialog, error.what(), "Cupid-N64 imported settings", MB_OK | MB_ICONERROR);
    SendDlgItemMessageW(content(dialog, *review), list_id, LB_SETCURSEL, review->selected, 0);
  }
  return TRUE;
}

} // namespace

std::optional<SettingsTransfer> review_settings_paths(HWND owner, SettingsTransfer settings) {
  auto paths = settings.missing_paths();
  if (paths.empty())
    return settings;
  Review review{std::move(settings), std::move(paths), 0, {}};
  const auto data = dialog_template(L"Review imported settings paths", 450, 244);
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&review));
  if (result == -1)
    throw std::runtime_error("Could not open the imported settings path review.");
  return result == IDOK ? std::optional(std::move(review.settings)) : std::nullopt;
}

} // namespace cupid::desktop
