#include "desktop/debug/format.hpp"
#include "desktop/session.hpp"
#include "desktop/windows/debug/controls.hpp"
#include "desktop/windows/debug/dialogs.hpp"
#include <algorithm>
#include <sstream>

namespace cupid::desktop {
namespace {

using namespace inspection_controls;
constexpr unsigned region_id = 100, address_id = 101, dump_id = 102, bytes_id = 103;
constexpr unsigned refresh_id = 104, write_id = 105, previous_id = 106, next_id = 107;
constexpr unsigned details_id = 108, notice_id = 109;
constexpr unsigned page_size = 256;

struct MemoryDialog {
  Session &session;
  std::vector<n64::MemoryRegion> regions;
  ScrollForm scroll;
  unsigned selected = 0;
  std::uint32_t offset = 0;
  bool changed = false;

  void address(HWND dialog) const {
    std::ostringstream formatted;
    formatted << std::hex << regions[selected].address.value_or(0) + offset;
    text(dialog, scroll, address_id, formatted.str());
  }
  void refresh(HWND dialog) {
    const auto &region = regions[selected];
    offset = inspection_offset(region, inspection_address(text(dialog, scroll, address_id)));
    const auto bytes =
        session.read_memory(region.id, offset, std::min(page_size, region.size - offset));
    text(dialog, scroll, dump_id, inspection_dump(region.address.value_or(0) + offset, bytes));
    std::ostringstream details;
    details << region.access << ".\r\nBig endian; ";
    details << (region.address ? "address range " : "offset range ") << std::hex
            << region.address.value_or(0);
    details << " - " << region.address.value_or(0) + region.size - 1 << "; ";
    details << (region.writable ? "editable while paused." : "read-only.");
    text(dialog, scroll, details_id, details.str());
    EnableWindow(field(dialog, scroll, write_id), region.writable);
    EnableWindow(field(dialog, scroll, bytes_id), region.writable);
    EnableWindow(field(dialog, scroll, previous_id), offset != 0);
    EnableWindow(field(dialog, scroll, next_id), region.size - offset > page_size);
  }
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *model = reinterpret_cast<MemoryDialog *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    model = reinterpret_cast<MemoryDialog *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, region_id,
                   10, 10, 285, 180);
    for (const auto &region : model->regions) {
      const auto name = wide(region.name);
      SendDlgItemMessageW(dialog, region_id, CB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendDlgItemMessageW(dialog, region_id, CB_SETCURSEL, 0, 0);
    dialog_control(dialog, L"STATIC", L"Address / offset (hex)", 0, 110, 10, 37, 130, 12);
    dialog_control(dialog, L"EDIT", L"0", ES_AUTOHSCROLL | WS_TABSTOP, address_id, 140, 35, 170,
                   17);
    dialog_control(dialog, L"BUTTON", L"Inspect", BS_DEFPUSHBUTTON | WS_TABSTOP, refresh_id, 320,
                   35, 65, 17);
    dialog_control(dialog, L"BUTTON", L"Previous", BS_PUSHBUTTON | WS_TABSTOP, previous_id, 390, 35,
                   60, 17);
    dialog_control(dialog, L"BUTTON", L"Next", BS_PUSHBUTTON | WS_TABSTOP, next_id, 455, 35, 45,
                   17);
    dialog_control(dialog, L"STATIC", L"", 0, details_id, 10, 59, 490, 30);
    monospace(dialog_control(dialog, L"EDIT", L"",
                             ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
                                 WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                             dump_id, 10, 92, 490, 164));
    dialog_control(dialog, L"STATIC", L"Bytes to write at the selected address (hex pairs)", 0, 111,
                   10, 266, 360, 12);
    monospace(dialog_control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, bytes_id, 10, 282,
                             400, 17));
    dialog_control(dialog, L"BUTTON", L"Write bytes", BS_PUSHBUTTON | WS_TABSTOP, write_id, 420,
                   282, 80, 17);
    dialog_control(dialog, L"STATIC", L"Emulation stays paused when this window closes.", 0,
                   notice_id, 10, 305, 490, 28);
    dialog_control(dialog, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 440, 340, 60,
                   17);
    model->scroll.initialize(dialog, 340);
    model->address(dialog);
    center_dialog(dialog);
  } else if (!model)
    return FALSE;
  else if (model->scroll.message(event, wparam))
    return TRUE;
  else if (event != WM_COMMAND)
    return FALSE;
  const auto id = event == WM_INITDIALOG ? refresh_id : LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  try {
    if (id == region_id && HIWORD(wparam) == CBN_SELCHANGE) {
      const auto selected =
          SendMessageW(field(dialog, model->scroll, region_id), CB_GETCURSEL, 0, 0);
      if (selected < 0 || std::size_t(selected) >= model->regions.size())
        return TRUE;
      model->selected = static_cast<unsigned>(selected);
      model->offset = 0;
      model->address(dialog);
      text(dialog, model->scroll, bytes_id, "");
    } else if (id == previous_id || id == next_id) {
      const auto &region = model->regions[model->selected];
      model->offset =
          inspection_offset(region, inspection_address(text(dialog, model->scroll, address_id)));
      if (id == previous_id)
        model->offset -= std::min(model->offset, page_size);
      else if (region.size - model->offset > page_size)
        model->offset += page_size;
      model->address(dialog);
    } else if (id == write_id) {
      const auto &region = model->regions[model->selected];
      const auto offset =
          inspection_offset(region, inspection_address(text(dialog, model->scroll, address_id)));
      const auto bytes = inspection_bytes(text(dialog, model->scroll, bytes_id));
      model->session.write_memory(region.id, offset, bytes);
      model->changed = true;
      text(dialog, model->scroll, notice_id, "Bytes written. Emulation remains paused.");
    } else if (id != refresh_id)
      return FALSE;
    model->refresh(dialog);
  } catch (const std::exception &error) {
    text(dialog, model->scroll, notice_id, error.what());
  }
  return TRUE;
}

} // namespace

bool show_memory_inspection(HWND owner, Session &session) {
  MemoryDialog model{session, session.memory_regions(), {}, 0, 0, false};
  const auto data = dialog_template(L"N64 memory inspection", 510, 367);
  if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                              reinterpret_cast<const DLGTEMPLATE *>(data.data()), owner, procedure,
                              reinterpret_cast<LPARAM>(&model)) == -1)
    throw std::runtime_error("Could not open memory inspection.");
  return model.changed;
}

} // namespace cupid::desktop
