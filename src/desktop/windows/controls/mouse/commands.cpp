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
  mouse_.release();
  if (session_)
    session_->clear_mouse_input();
  update_shortcuts();
}

void Window::mouse_capture() {
  if (mouse_.captured()) {
    release_mouse();
    state_message_ = L"Mouse pointer released";
  } else if (has_mouse() && !stopped() && !rewinding_ && active_) {
    RECT client{};
    GetClientRect(window_, &client);
    client.bottom -= status_height();
    mouse_.acquire(window_, client);
    if (mouse_.captured())
      state_message_ = L"Mouse captured | Escape releases the pointer";
  }
  update_shortcuts();
  InvalidateRect(window_, nullptr, FALSE);
}

} // namespace cupid::desktop
