#include "desktop/windows/window.hpp"

namespace cupid::desktop {

bool Window::has_mouse() const {
  if (session_)
    for (unsigned port = 0; port < 4; ++port)
      if (session_->port_status(port).controller == ControllerType::Mouse)
        return true;
  return false;
}

void Window::release_mouse() {
  mouse_capture_pending_ = false;
  mouse_.release();
  if (session_)
    session_->clear_mouse_input();
  update_shortcuts();
}

void Window::mouse_capture() {
  if (mouse_.captured() || mouse_capture_pending_) {
    release_mouse();
    state_message_ = L"Mouse pointer released";
  } else if (has_mouse() && !paused_ && !rewinding_ && active_) {
    if (modal_) {
      mouse_capture_pending_ = true;
    } else {
      RECT client{};
      GetClientRect(window_, &client);
      client.bottom -= status_height();
      mouse_.acquire(window_, client);
      if (mouse_.captured())
        state_message_ = L"Mouse captured | Escape releases the pointer";
    }
  }
  update_shortcuts();
  InvalidateRect(window_, nullptr, FALSE);
}

} // namespace cupid::desktop
