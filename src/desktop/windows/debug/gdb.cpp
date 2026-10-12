#include "desktop/windows/commands.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include "desktop/windows/window.hpp"
#include <charconv>

namespace cupid::desktop {
namespace {

struct Connection {
  bool enabled;
  unsigned port;
  unsigned listening;
};

INT_PTR CALLBACK connection_procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *model = reinterpret_cast<Connection *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    model = reinterpret_cast<Connection *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    const auto check = dialog_control(dialog, L"BUTTON", L"Enable GDB connection",
                                      BS_AUTOCHECKBOX | WS_TABSTOP, 100, 10, 10, 220, 18);
    SendMessageW(check, BM_SETCHECK, model->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    dialog_control(dialog, L"STATIC", L"Port (1 to 65535)", 0, 102, 10, 39, 105, 18);
    dialog_control(dialog, L"EDIT", std::to_wstring(model->port).c_str(),
                   ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 101, 120, 37, 85, 18);
    SendDlgItemMessageW(dialog, 101, EM_SETLIMITTEXT, 5, 0);
    const auto status = model->listening
                            ? L"Listening on 127.0.0.1:" + std::to_wstring(model->listening)
                            : L"Connection disabled";
    dialog_control(dialog, L"STATIC", status.c_str(), 0, 103, 10, 66, 320, 18);
    dialog_control(
        dialog, L"STATIC",
        L"Use a MIPS-capable GDB with:\nset endian big\ntarget remote 127.0.0.1:<port>\n\n"
        L"Connecting pauses the game. Use GDB to step or continue. Detaching restores the prior "
        L"pause setting.",
        0, 104, 10, 90, 320, 94);
    dialog_control(dialog, L"BUTTON", L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 190, 193, 65,
                   18);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 265, 193, 65,
                   18);
    center_dialog(dialog);
    return TRUE;
  }
  if (event != WM_COMMAND || !model)
    return FALSE;
  const auto id = LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  if (id != IDOK)
    return FALSE;
  BOOL valid = FALSE;
  const auto port = GetDlgItemInt(dialog, 101, &valid, FALSE);
  if (!valid || !port || port > 65535) {
    MessageBoxW(dialog, L"Enter a port from 1 to 65535.", L"GDB connection", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  model->port = port;
  model->enabled = SendDlgItemMessageW(dialog, 100, BM_GETCHECK, 0, 0) == BST_CHECKED;
  EndDialog(dialog, IDOK);
  return TRUE;
}

} // namespace

void Window::load_gdb_settings() {
  gdb_enabled_ = !options_.frames && !settings_.empty() &&
                 GetPrivateProfileIntW(L"Developer", L"GDB enabled", 0, settings_.c_str()) != 0;
  gdb_port_ = settings_.empty()
                  ? 2345
                  : GetPrivateProfileIntW(L"Developer", L"GDB port", 2345, settings_.c_str());
  if (!gdb_port_ || gdb_port_ > 65535)
    gdb_port_ = 2345;
}

bool Window::gdb_command(unsigned id) {
  if (id != GdbSettings)
    return false;
  clear_host_input();
  try {
    Connection model{gdb_enabled_, gdb_port_, gdb_.port()};
    const auto data = dialog_template(L"GDB connection", 340, 224);
    const auto result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE *>(data.data()), window_,
        connection_procedure, reinterpret_cast<LPARAM>(&model));
    if (result == -1)
      throw std::runtime_error("Could not open GDB connection settings.");
    if (result == IDOK) {
      if (model.enabled)
        gdb_.listen(model.port);
      else
        gdb_.close();
      gdb_enabled_ = model.enabled;
      gdb_port_ = model.port;
      poll_gdb();
      if (!settings_.empty() && !options_.frames) {
        if (!WritePrivateProfileStringW(L"Developer", L"GDB enabled", gdb_enabled_ ? L"1" : L"0",
                                        settings_.c_str()) ||
            !WritePrivateProfileStringW(L"Developer", L"GDB port",
                                        std::to_wstring(gdb_port_).c_str(), settings_.c_str()))
          throw std::runtime_error("Could not save GDB connection settings.");
      }
      state_message_ = gdb_enabled_ ? L"GDB listening on 127.0.0.1:" + std::to_wstring(gdb_port_)
                                    : L"GDB connection disabled";
    }
  } catch (const std::exception &error) {
    status_error(L"GDB connection", error);
  }
  rebase();
  return true;
}

void Window::poll_gdb() {
  try {
    gdb_.poll();
  } catch (const std::exception &error) {
    status_error(L"GDB connection", error);
  }
  const bool connected = gdb_.connected();
  const bool changed = connected != gdb_connected_;
  if (connected && !gdb_connected_) {
    gdb_was_paused_ = paused_;
    end_rewind();
    stop_fast_forward();
    clear_host_input();
    gamepads_.stop();
    if (rewind_)
      rewind_->clear();
  }
  const bool paused = connected ? session_->debugger().halted()
                      : changed ? gdb_was_paused_
                                : paused_;
  gdb_connected_ = connected;
  if (changed || paused != paused_) {
    paused_ = paused;
    audio_.clear();
    if (connected && paused) {
      session_->finish_frame();
      frame_ready_ = false;
      last_frame_ = session_->frames;
      refresh_frame();
    }
    state_message_ =
        connected ? paused ? L"Debugger paused" : L"Debugger running" : L"Debugger disconnected";
    rebase();
    update_playback();
  }
}

} // namespace cupid::desktop
