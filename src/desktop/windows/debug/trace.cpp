#include "desktop/debug/format.hpp"
#include "desktop/session.hpp"
#include "desktop/windows/debug/controls.hpp"
#include "desktop/windows/debug/dialogs.hpp"
#include <charconv>
#include <iomanip>
#include <sstream>

namespace cupid::desktop {
namespace {

using namespace inspection_controls;
using n64::TraceKind;
constexpr unsigned capacity_id = 105, cpu_first_id = 106, cpu_last_id = 107;
constexpr unsigned rsp_first_id = 108, rsp_last_id = 109, start_id = 110, stop_id = 111;
constexpr unsigned clear_id = 112, refresh_id = 113, dump_id = 114, notice_id = 115;
constexpr std::array<std::uint32_t, 5> categories{
    n64::trace_bit(TraceKind::CpuInstruction), n64::trace_bit(TraceKind::RspInstruction),
    n64::trace_bit(TraceKind::Exception) | n64::trace_bit(TraceKind::Nmi),
    n64::trace_bit(TraceKind::Interrupt), n64::trace_bit(TraceKind::Tlb)};

struct TraceDialog {
  Session &session;
  ScrollForm scroll;

  void refresh(HWND dialog) {
    const auto &trace = session.tracing();
    const auto records = trace.records();
    text(dialog, scroll, dump_id, trace_dump(records));
    std::ostringstream status;
    status << (trace.config().mask ? "Capturing" : "Stopped") << "; " << records.size()
           << " retained, " << trace.dropped() << " overwritten. Showing the latest 256.\r\n"
           << "Close this window and resume emulation to collect instructions. Reopen to inspect "
              "or stop.";
    text(dialog, scroll, notice_id, status.str());
  }
  void configure(HWND dialog) {
    auto config = session.tracing().config();
    config.mask = 0;
    for (unsigned n = 0; n < categories.size(); ++n)
      if (SendMessageW(field(dialog, scroll, 100 + n), BM_GETCHECK, 0, 0) == BST_CHECKED)
        config.mask |= categories[n];
    if (!config.mask)
      throw std::runtime_error("Select at least one trace category.");
    const auto capacity = text(dialog, scroll, capacity_id);
    const auto parsed =
        std::from_chars(capacity.data(), capacity.data() + capacity.size(), config.capacity);
    if (parsed.ec != std::errc() || parsed.ptr != capacity.data() + capacity.size())
      throw std::runtime_error("Enter the record capacity as a decimal number from 1 to 65536.");
    config.cpu = {instruction_address(n64::DebugProcessor::Cpu, text(dialog, scroll, cpu_first_id)),
                  instruction_address(n64::DebugProcessor::Cpu, text(dialog, scroll, cpu_last_id))};
    config.rsp = {inspection_address(text(dialog, scroll, rsp_first_id)),
                  inspection_address(text(dialog, scroll, rsp_last_id))};
    session.tracing().configure(config);
  }
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *model = reinterpret_cast<TraceDialog *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    model = reinterpret_cast<TraceDialog *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    const auto config = model->session.tracing().config();
    constexpr std::array<const wchar_t *, 5> names{L"CPU instructions", L"RSP instructions",
                                                   L"Exceptions / NMI", L"Interrupts",
                                                   L"TLB events"};
    for (unsigned n = 0; n < names.size(); ++n) {
      const auto check = dialog_control(dialog, L"BUTTON", names[n], BS_AUTOCHECKBOX | WS_TABSTOP,
                                        100 + n, 10 + static_cast<int>(n) * 112, 10, 110, 17);
      const bool enabled = config.mask ? (config.mask & categories[n]) != 0 : n != 1;
      SendMessageW(check, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    constexpr std::array<const wchar_t *, 5> labels{L"CPU start (hex)", L"CPU end (hex)",
                                                    L"RSP start (hex)", L"RSP end (hex)",
                                                    L"Record capacity (decimal)"};
    constexpr std::array<unsigned, 5> ids{cpu_first_id, cpu_last_id, rsp_first_id, rsp_last_id,
                                          capacity_id};
    for (unsigned n = 0; n < labels.size(); ++n) {
      const auto row = 36 + static_cast<int>(n) * 24;
      dialog_control(dialog, L"STATIC", labels[n], 0, 120 + n, 10, row + 2, 145, 14);
      dialog_control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, ids[n], 165, row, 180, 17);
    }
    const auto hex_text = [&](unsigned id, std::uint64_t value) {
      std::ostringstream out;
      out << std::hex << std::setfill('0')
          << std::setw(id == cpu_first_id || id == cpu_last_id ? 16 : 3) << value;
      SetDlgItemTextW(dialog, id, wide(out.str()).c_str());
    };
    hex_text(cpu_first_id, config.cpu.first);
    hex_text(cpu_last_id, config.cpu.last);
    hex_text(rsp_first_id, config.rsp.first);
    hex_text(rsp_last_id, config.rsp.last);
    SetDlgItemTextW(dialog, capacity_id, std::to_wstring(config.capacity).c_str());
    dialog_control(dialog, L"BUTTON", L"Start / clear log", BS_DEFPUSHBUTTON | WS_TABSTOP, start_id,
                   365, 36, 100, 18);
    dialog_control(dialog, L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, stop_id, 475, 36, 80,
                   18);
    dialog_control(dialog, L"BUTTON", L"Clear log", BS_PUSHBUTTON | WS_TABSTOP, clear_id, 365, 65,
                   100, 18);
    dialog_control(dialog, L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP, refresh_id, 475, 65,
                   80, 18);
    monospace(dialog_control(dialog, L"EDIT", L"",
                             ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
                                 WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                             dump_id, 10, 164, 545, 230));
    dialog_control(dialog, L"STATIC", L"", 0, notice_id, 10, 402, 545, 40);
    dialog_control(dialog, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 495, 449, 60,
                   17);
    model->scroll.initialize(dialog, 449);
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
    if (id == start_id)
      model->configure(dialog);
    else if (id == stop_id)
      model->session.tracing().stop();
    else if (id == clear_id)
      model->session.tracing().clear();
    else if (id != refresh_id)
      return FALSE;
    model->refresh(dialog);
  } catch (const std::exception &error) {
    text(dialog, model->scroll, notice_id, error.what());
  }
  return TRUE;
}

} // namespace

void show_instruction_trace(HWND owner, Session &session) {
  TraceDialog model{session, {}};
  const auto data = dialog_template(L"CPU and RSP instruction trace", 565, 476);
  if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                              reinterpret_cast<const DLGTEMPLATE *>(data.data()), owner, procedure,
                              reinterpret_cast<LPARAM>(&model)) == -1)
    throw std::runtime_error("Could not open instruction tracing.");
}

} // namespace cupid::desktop
