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
constexpr unsigned apply_id = 108, stop_id = 109, clear_id = 110, reset_id = 111;
constexpr unsigned refresh_id = 112, output_id = 113, notice_id = 114;
constexpr unsigned first_id = 300, last_id = 301, cpu_first_id = 302, cpu_last_id = 303;
constexpr unsigned rsp_first_id = 304, rsp_last_id = 305, capacity_id = 306;

struct DiagnosticDialog {
  Session &session;
  ScrollForm scroll;

  void refresh(HWND dialog) {
    const auto &diagnostics = session.diagnostics();
    text(dialog, scroll, output_id,
         profile_dump(diagnostics.profile()) + "\r\n" + diagnostic_dump(diagnostics.records()));
    const auto status =
        std::string(diagnostics.instrumented() || diagnostics.config().devices ? "Capturing; "
                                                                               : "Stopped; ") +
        std::to_string(diagnostics.records().size()) + " retained, " +
        std::to_string(diagnostics.dropped()) + " overwritten. " +
        "Showing up to 256 recent records within 64 KiB.\r\n"
        "Close and resume to collect data. EMUX applies only to this session.";
    text(dialog, scroll, notice_id, status);
  }
  void configure(HWND dialog) {
    auto config = session.diagnostics().config();
    const auto checked = [&](unsigned id) {
      return SendMessageW(field(dialog, scroll, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    config.profiling = checked(100);
    config.coherence = checked(101);
    config.devices = 0;
    for (unsigned n = 0; n < 12; ++n)
      if (checked(200 + n))
        config.devices |= 1u << n;
    const auto capacity = text(dialog, scroll, capacity_id);
    const auto parsed =
        std::from_chars(capacity.data(), capacity.data() + capacity.size(), config.capacity);
    if (parsed.ec != std::errc() || parsed.ptr != capacity.data() + capacity.size())
      throw std::runtime_error("Enter a decimal capacity from 1 to 65536.");
    const auto address = [&](unsigned id) { return inspection_address(text(dialog, scroll, id)); };
    const auto first = address(first_id), last = address(last_id);
    if (first > ~0u || last > ~0u)
      throw std::runtime_error("Physical address filters must fit in 32 bits.");
    config.first_address = static_cast<std::uint32_t>(first);
    config.last_address = static_cast<std::uint32_t>(last);
    config.cpu = {instruction_address(n64::DebugProcessor::Cpu, text(dialog, scroll, cpu_first_id)),
                  instruction_address(n64::DebugProcessor::Cpu, text(dialog, scroll, cpu_last_id))};
    config.rsp = {address(rsp_first_id), address(rsp_last_id)};
    session.diagnostics().configure(config);
    session.diagnostics().set_homebrew(checked(102));
  }
};

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *model = reinterpret_cast<DiagnosticDialog *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    model = reinterpret_cast<DiagnosticDialog *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    const auto config = model->session.diagnostics().config();
    constexpr std::array<const wchar_t *, 3> modes{L"Profile counters", L"Cache / DMA warnings",
                                                   L"Enable EMUX homebrew"};
    const std::array enabled{config.profiling, config.coherence,
                             model->session.diagnostics().homebrew()};
    for (unsigned n = 0; n < modes.size(); ++n) {
      const auto check = dialog_control(dialog, L"BUTTON", modes[n], BS_AUTOCHECKBOX | WS_TABSTOP,
                                        100 + n, 10 + int(n) * 183, 10, 181, 17);
      SendMessageW(check, BM_SETCHECK, enabled[n] ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    constexpr std::array<const wchar_t *, 12> devices{L"MI",  L"VI",  L"Audio",     L"PI",
                                                      L"SI",  L"RI",  L"RDRAM",     L"PIF",
                                                      L"RDP", L"RSP", L"Cartridge", L"Disk drive"};
    for (unsigned n = 0; n < devices.size(); ++n) {
      const auto check =
          dialog_control(dialog, L"BUTTON", devices[n], BS_AUTOCHECKBOX | WS_TABSTOP, 200 + n,
                         10 + int(n % 6) * 92, 35 + int(n / 6) * 22, 90, 17);
      SendMessageW(check, BM_SETCHECK, config.devices & (1u << n) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    constexpr std::array<const wchar_t *, 7> labels{
        L"Physical start (hex)",     L"Physical end (hex)", L"CPU PC start (hex)",
        L"CPU PC end (hex)",         L"RSP PC start (hex)", L"RSP PC end (hex)",
        L"Record capacity (decimal)"};
    const std::array<std::uint64_t, 7> values{
        config.first_address, config.last_address, config.cpu.first, config.cpu.last,
        config.rsp.first,     config.rsp.last,     config.capacity};
    for (unsigned n = 0; n < labels.size(); ++n) {
      const auto y = 82 + int(n) * 24;
      dialog_control(dialog, L"STATIC", labels[n], 0, 400 + n, 10, y + 2, 145, 14);
      dialog_control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 300 + n, 160, y, 190, 17);
      std::ostringstream value;
      if (n < 6)
        value << std::hex;
      value << values[n];
      SetDlgItemTextW(dialog, 300 + n, wide(value.str()).c_str());
    }
    constexpr std::array<const wchar_t *, 5> buttons{L"Apply / clear log", L"Stop capture",
                                                     L"Clear log", L"Reset counters", L"Refresh"};
    for (unsigned n = 0; n < buttons.size(); ++n)
      dialog_control(dialog, L"BUTTON", buttons[n], BS_PUSHBUTTON | WS_TABSTOP, 108 + n, 375,
                     82 + int(n) * 29, 175, 18);
    monospace(dialog_control(dialog, L"EDIT", L"",
                             ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
                                 WS_HSCROLL | WS_VSCROLL | WS_TABSTOP,
                             output_id, 10, 258, 545, 190));
    dialog_control(dialog, L"STATIC", L"", 0, notice_id, 10, 454, 545, 42);
    dialog_control(dialog, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 495, 501, 60,
                   17);
    model->scroll.initialize(dialog, 501);
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
    if (id == apply_id)
      model->configure(dialog);
    else if (id == stop_id)
      model->session.diagnostics().stop();
    else if (id == clear_id)
      model->session.diagnostics().clear_log();
    else if (id == reset_id)
      model->session.diagnostics().clear_profiles();
    else if (id != refresh_id)
      return FALSE;
    model->refresh(dialog);
  } catch (const std::exception &error) {
    text(dialog, model->scroll, notice_id, error.what());
  }
  return TRUE;
}

} // namespace

void show_core_diagnostics(HWND owner, Session &session) {
  DiagnosticDialog model{session, {}};
  const auto data = dialog_template(L"Core diagnostics and EMUX homebrew", 565, 528);
  if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                              reinterpret_cast<const DLGTEMPLATE *>(data.data()), owner, procedure,
                              reinterpret_cast<LPARAM>(&model)) == -1)
    throw std::runtime_error("Could not open core diagnostics.");
}

} // namespace cupid::desktop
