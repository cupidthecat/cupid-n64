#include "desktop/debug/format.hpp"
#include "desktop/session.hpp"
#include "desktop/windows/debug/controls.hpp"
#include "desktop/windows/debug/dialogs.hpp"
#include <iomanip>
#include <sstream>

namespace cupid::desktop {
namespace {

using namespace inspection_controls;
constexpr unsigned processor_id = 100, address_id = 101, dump_id = 102, current_id = 103;
constexpr unsigned refresh_id = 104, previous_id = 105, next_id = 106, notice_id = 107;
constexpr unsigned rows = 24;

struct DisassemblyDialog {
  Session &session;
  ScrollForm scroll;
  n64::DebugProcessor processor = n64::DebugProcessor::Cpu;
  std::uint64_t address = 0;

  void set_address(HWND dialog) const {
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(processor == n64::DebugProcessor::Cpu ? 16 : 3) << address;
    text(dialog, scroll, address_id, out.str());
  }
  void refresh(HWND dialog) {
    address = instruction_address(processor, text(dialog, scroll, address_id));
    if ((address & 3) || (processor == n64::DebugProcessor::Rsp && address > 0xfff))
      throw std::runtime_error("Enter an aligned CPU address or an RSP address from 0 to FFC.");
    std::string out;
    auto pc = address;
    const auto current = session.instruction_pc(processor);
    for (unsigned n = 0; n < rows; ++n) {
      out += pc == current ? "> " : "  ";
      out += instruction_row(processor, pc, session.instruction(processor, pc)) + "\r\n";
      if (processor == n64::DebugProcessor::Rsp)
        pc = (pc + 4) & 0xfff;
      else if (pc <= ~std::uint64_t(0) - 4)
        pc += 4;
      else
        break;
    }
    text(dialog, scroll, dump_id, out);
    text(dialog, scroll, notice_id,
         "The > marker identifies the current PC. Emulation stays paused when this window closes.");
  }
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *model = reinterpret_cast<DisassemblyDialog *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    model = reinterpret_cast<DisassemblyDialog *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, processor_id, 10, 10,
                   90, 70);
    for (const auto name : {L"CPU", L"RSP"})
      SendDlgItemMessageW(dialog, processor_id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendDlgItemMessageW(dialog, processor_id, CB_SETCURSEL, 0, 0);
    dialog_control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, address_id, 110, 10, 180, 17);
    dialog_control(dialog, L"BUTTON", L"Current PC", BS_PUSHBUTTON | WS_TABSTOP, current_id, 300,
                   10, 65, 17);
    dialog_control(dialog, L"BUTTON", L"Inspect", BS_DEFPUSHBUTTON | WS_TABSTOP, refresh_id, 375,
                   10, 60, 17);
    dialog_control(dialog, L"BUTTON", L"Previous", BS_PUSHBUTTON | WS_TABSTOP, previous_id, 440, 10,
                   60, 17);
    dialog_control(dialog, L"BUTTON", L"Next", BS_PUSHBUTTON | WS_TABSTOP, next_id, 505, 10, 50,
                   17);
    monospace(dialog_control(dialog, L"EDIT", L"",
                             ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
                                 WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                             dump_id, 10, 40, 545, 260));
    dialog_control(dialog, L"STATIC", L"", 0, notice_id, 10, 308, 545, 30);
    dialog_control(dialog, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 495, 345, 60,
                   17);
    model->scroll.initialize(dialog, 345);
    model->address = model->session.instruction_pc(model->processor);
    model->set_address(dialog);
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
    if (id == processor_id && HIWORD(wparam) == CBN_SELCHANGE) {
      const auto selected =
          SendMessageW(field(dialog, model->scroll, processor_id), CB_GETCURSEL, 0, 0);
      if (selected < 0 || selected > 1)
        return TRUE;
      model->processor = selected ? n64::DebugProcessor::Rsp : n64::DebugProcessor::Cpu;
      model->address = model->session.instruction_pc(model->processor);
      model->set_address(dialog);
    } else if (id == current_id) {
      model->address = model->session.instruction_pc(model->processor);
      model->set_address(dialog);
    } else if (id == previous_id || id == next_id) {
      const auto address =
          instruction_address(model->processor, text(dialog, model->scroll, address_id));
      constexpr auto stride = rows * 4;
      if (model->processor == n64::DebugProcessor::Rsp)
        model->address = (address + (id == next_id ? stride : 4096 - stride)) & 0xfff;
      else
        model->address =
            id == previous_id
                ? address - std::min<std::uint64_t>(address, stride)
                : address + std::min<std::uint64_t>(~std::uint64_t(0) - address, stride);
      model->set_address(dialog);
    } else if (id != refresh_id)
      return FALSE;
    model->refresh(dialog);
  } catch (const std::exception &error) {
    text(dialog, model->scroll, notice_id, error.what());
  }
  return TRUE;
}

} // namespace

void show_disassembly(HWND owner, Session &session) {
  DisassemblyDialog model{session, {}};
  const auto data = dialog_template(L"CPU and RSP disassembly", 565, 372);
  if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                              reinterpret_cast<const DLGTEMPLATE *>(data.data()), owner, procedure,
                              reinterpret_cast<LPARAM>(&model)) == -1)
    throw std::runtime_error("Could not open disassembly.");
}

} // namespace cupid::desktop
