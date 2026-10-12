#include "desktop/debug/format.hpp"
#include "desktop/session.hpp"
#include "desktop/windows/debug/controls.hpp"
#include "desktop/windows/debug/dialogs.hpp"

namespace cupid::desktop {
namespace {

using namespace inspection_controls;
struct PropertiesDialog {
  const Session &session;
  ScrollForm scroll;
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *model = reinterpret_cast<PropertiesDialog *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    model = reinterpret_cast<PropertiesDialog *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 100, 10,
                   10, 350, 200);
    for (const auto &device : n64::Inspection::devices()) {
      const auto name = wide(device.name);
      SendDlgItemMessageW(dialog, 100, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendDlgItemMessageW(dialog, 100, CB_SETCURSEL, 0, 0);
    dialog_control(dialog, L"BUTTON", L"Refresh", BS_DEFPUSHBUTTON | WS_TABSTOP, 104, 370, 10, 65,
                   17);
    dialog_control(dialog, L"STATIC",
                   L"Read-only snapshot. Refresh reads state without accessing the device bus.", 0,
                   110, 10, 37, 490, 25);
    monospace(dialog_control(dialog, L"EDIT", L"",
                             ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
                                 WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                             102, 10, 65, 490, 260));
    dialog_control(dialog, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 440, 340, 60,
                   17);
    model->scroll.initialize(dialog, 340);
    center_dialog(dialog);
  } else if (!model)
    return FALSE;
  else if (model->scroll.message(event, wparam))
    return TRUE;
  else if (event != WM_COMMAND)
    return FALSE;
  const auto id = event == WM_INITDIALOG ? 104 : LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  if (id != 104 && !(id == 100 && HIWORD(wparam) == CBN_SELCHANGE))
    return FALSE;
  try {
    const auto selected = SendMessageW(field(dialog, model->scroll, 100), CB_GETCURSEL, 0, 0);
    const auto devices = n64::Inspection::devices();
    if (selected < 0 || std::size_t(selected) >= devices.size())
      return TRUE;
    text(dialog, model->scroll, 102,
         inspection_properties(model->session.device_properties(devices[selected].id)));
  } catch (const std::exception &error) {
    text(dialog, model->scroll, 102, error.what());
  }
  return TRUE;
}

} // namespace

void show_device_properties(HWND owner, const Session &session) {
  PropertiesDialog model{session, {}};
  const auto data = dialog_template(L"N64 device properties", 510, 367);
  if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                              reinterpret_cast<const DLGTEMPLATE *>(data.data()), owner, procedure,
                              reinterpret_cast<LPARAM>(&model)) == -1)
    throw std::runtime_error("Could not open device properties.");
}

} // namespace cupid::desktop
