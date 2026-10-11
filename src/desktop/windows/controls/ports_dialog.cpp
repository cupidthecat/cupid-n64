#include "desktop/windows/controls/ports_dialog.hpp"
#include "core/controller/accessories/cartridge/handheld.hpp"
#include "desktop/storage/save.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/dialogs/scroll.hpp"
#include <stdexcept>

namespace cupid::desktop {
namespace {

constexpr unsigned controller_first = 100, accessory_first = 200, bpm_first = 300,
                   media_first = 400, browse_first = 500, clear_first = 600;

struct DialogState {
  PortForm form;
  ScrollForm layout;
};

std::wstring description(const PortStatus &status) {
  auto text = std::wstring(controller_names.at(static_cast<unsigned>(status.controller)));
  if (status.controller == ControllerType::Nintendo64)
    text += L" / " + std::wstring(accessory_names.at(static_cast<unsigned>(status.accessory)));
  return text;
}

void refresh(HWND dialog, const PortForm &form) {
  const auto resolved = resolve_ports(form.settings, form.defaults);
  for (unsigned port = 0; port < form.settings.size(); ++port) {
    const bool pad = resolved[port].controller == ControllerType::Nintendo64;
    EnableWindow(GetDlgItem(dialog, accessory_first + port),
                 form.settings[port].controller == ControllerType::Automatic || pad);
    EnableWindow(GetDlgItem(dialog, bpm_first + port),
                 pad && resolved[port].accessory == AccessoryType::BioSensor);
    for (unsigned first : {media_first, browse_first, clear_first})
      EnableWindow(GetDlgItem(dialog, first + port),
                   pad && resolved[port].accessory == AccessoryType::Transfer);
    SetDlgItemTextW(dialog, media_first + port, form.settings[port].cartridge.c_str());
  }
}

void read(HWND dialog, PortForm &form) {
  for (unsigned port = 0; port < form.settings.size(); ++port) {
    auto &value = form.settings[port];
    value.controller = static_cast<ControllerType>(
        SendDlgItemMessageW(dialog, controller_first + port, CB_GETCURSEL, 0, 0));
    value.accessory = static_cast<AccessoryType>(
        SendDlgItemMessageW(dialog, accessory_first + port, CB_GETCURSEL, 0, 0));
    BOOL valid = FALSE;
    value.heart_rate = GetDlgItemInt(dialog, bpm_first + port, &valid, FALSE);
    if (!valid)
      throw std::runtime_error("Enter a Bio Sensor heart rate from 30 through 180 BPM.");
  }
  validate_ports(form.settings);
  const auto resolved = resolve_ports(form.settings, form.defaults);
  for (const auto &port : resolved) {
    if (port.cartridge.empty())
      continue;
    n64::HandheldCartridge cartridge;
    if (!cartridge.load(read_file(port.cartridge, 0x1000000)))
      throw std::runtime_error("Select a valid Game Boy or Game Boy Color cartridge image.");
  }
}

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dialog, DWLP_USER));
  auto *form = state ? &state->form : nullptr;
  if (event == WM_INITDIALOG) {
    state = reinterpret_cast<DialogState *>(lparam);
    form = &state->form;
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(
        dialog, L"STATIC",
        L"Controller and accessory choices are saved for this game and apply on reload.\n"
        L"Automatic uses the cartridge defaults. Existing accessory saves are kept.\n"
        L"Bio Sensor heart rate: 30-180 BPM; changes apply to a connected sensor now.",
        0, 0, 10, 8, 540, 38);
    for (unsigned port = 0; port < form->settings.size(); ++port) {
      const int y = 54 + int(port) * 66;
      const auto label = L"Port " + std::to_wstring(port + 1) + L": " +
                         description(form->current[port]) + L"  |  Automatic: " +
                         description(form->defaults[port]);
      dialog_control(dialog, L"STATIC", label.c_str(), 0, 0, 10, y, 540, 12);
      const auto controller =
          dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         controller_first + port, 10, y + 14, 150, 140);
      for (const auto name : controller_names)
        SendMessageW(controller, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.data()));
      SendMessageW(controller, CB_SETCURSEL, static_cast<unsigned>(form->settings[port].controller),
                   0);
      const auto accessory = dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                            accessory_first + port, 170, y + 14, 140, 140);
      for (const auto name : accessory_names)
        SendMessageW(accessory, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.data()));
      SendMessageW(accessory, CB_SETCURSEL, static_cast<unsigned>(form->settings[port].accessory),
                   0);
      dialog_control(dialog, L"STATIC", L"BPM:", 0, 0, 326, y + 17, 28, 12);
      dialog_control(dialog, L"EDIT", std::to_wstring(form->settings[port].heart_rate).c_str(),
                     ES_NUMBER | WS_TABSTOP, bpm_first + port, 358, y + 14, 50, 16);
      dialog_control(dialog, L"STATIC", L"Game Boy:", 0, 0, 10, y + 38, 50, 12);
      dialog_control(dialog, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP,
                     media_first + port, 65, y + 35, 355, 16);
      dialog_control(dialog, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP,
                     browse_first + port, 430, y + 35, 60, 16);
      dialog_control(dialog, L"BUTTON", L"Clear", BS_PUSHBUTTON | WS_TABSTOP, clear_first + port,
                     495, y + 35, 55, 16);
    }
    dialog_control(dialog, L"STATIC", L"Controller Pak capacities: Emulation > Memory settings.", 0,
                   0, 10, 313, 540, 12);
    dialog_control(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 440, 337, 50, 16);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 500, 337, 50,
                   16);
    refresh(dialog, *form);
    try {
      state->layout.initialize(dialog, 329);
    } catch (const std::exception &error) {
      MessageBoxA(dialog, error.what(), "Controller port settings", MB_OK | MB_ICONERROR);
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
  const auto controls = state->layout.content();
  const auto id = LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  try {
    if (id == IDOK) {
      read(controls, *form);
      EndDialog(dialog, IDOK);
    } else if (id >= controller_first && id < controller_first + 4 &&
               HIWORD(wparam) == CBN_SELCHANGE) {
      form->settings[id - controller_first].controller =
          static_cast<ControllerType>(SendDlgItemMessageW(controls, id, CB_GETCURSEL, 0, 0));
      refresh(controls, *form);
    } else if (id >= accessory_first && id < accessory_first + 4 &&
               HIWORD(wparam) == CBN_SELCHANGE) {
      form->settings[id - accessory_first].accessory =
          static_cast<AccessoryType>(SendDlgItemMessageW(controls, id, CB_GETCURSEL, 0, 0));
      refresh(controls, *form);
    } else if (id >= browse_first && id < browse_first + 4) {
      auto &path = form->settings[id - browse_first].cartridge;
      const auto directory = !path.empty() && std::filesystem::is_directory(path.parent_path())
                                 ? path.parent_path()
                                 : form->directory;
      if (const auto chosen = select_file(dialog, L"Select a Game Boy or Game Boy Color cartridge",
                                          FileSelection::Handheld, directory, path))
        path = *chosen;
      refresh(controls, *form);
    } else if (id >= clear_first && id < clear_first + 4) {
      form->settings[id - clear_first].cartridge.clear();
      refresh(controls, *form);
    }
  } catch (const std::exception &error) {
    MessageBoxA(dialog, error.what(), "Controller port settings", MB_OK | MB_ICONERROR);
  }
  return TRUE;
}

} // namespace

std::optional<ControllerPorts> choose_ports(HWND owner, PortForm form) {
  DialogState state{std::move(form), {}};
  const auto data = dialog_template(L"Controller ports for this game", 560, 363);
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&state));
  if (result == -1)
    throw std::runtime_error("Could not open controller port settings.");
  return result == IDOK ? std::optional(std::move(state.form.settings)) : std::nullopt;
}

} // namespace cupid::desktop
