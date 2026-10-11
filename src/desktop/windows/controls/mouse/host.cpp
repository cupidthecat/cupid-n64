#include "desktop/windows/controls/mouse/host.hpp"
#include <stdexcept>

namespace cupid::desktop {

void HostMouse::acquire(HWND window, RECT client) {
  if (captured() || GetForegroundWindow() != window)
    return;
  if (client.right <= client.left || client.bottom <= client.top)
    return;
  if (!GetClipCursor(&previous_clip_) || !GetCursorPos(&previous_position_))
    throw std::runtime_error("Could not read the pointer position before mouse capture.");
  const RAWINPUTDEVICE device{1, 2, RIDEV_NOLEGACY, window};
  if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
    throw std::runtime_error("Could not capture mouse input.");
  MapWindowPoints(window, nullptr, reinterpret_cast<POINT *>(&client), 2);
  window_ = window;
  SetCapture(window);
  if (!ClipCursor(&client)) {
    release();
    throw std::runtime_error("Could not confine the captured pointer to the game window.");
  }
  motion_.clear();
  absolute_.clear();
  left_ = right_ = false;
  do {
    ++hidden_;
  } while (ShowCursor(FALSE) >= 0);
}

void HostMouse::release() {
  const auto window = window_;
  window_ = nullptr;
  motion_.clear();
  absolute_.clear();
  left_ = right_ = false;
  if (!window)
    return;
  const RAWINPUTDEVICE device{1, 2, RIDEV_REMOVE, nullptr};
  RegisterRawInputDevices(&device, 1, sizeof(device));
  ClipCursor(&previous_clip_);
  if (GetCapture() == window)
    ReleaseCapture();
  while (hidden_) {
    ShowCursor(TRUE);
    --hidden_;
  }
  if (GetForegroundWindow() == window)
    SetCursorPos(previous_position_.x, previous_position_.y);
}

void HostMouse::raw_input(HRAWINPUT input) {
  if (!captured())
    return;
  RAWINPUT data{};
  UINT size = sizeof(data);
  if (GetRawInputData(input, RID_INPUT, &data, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) ||
      size < sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) || data.header.dwType != RIM_TYPEMOUSE)
    return;
  packet(data.data.mouse);
}

void HostMouse::packet(const RAWMOUSE &mouse) {
  if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
    left_ = true;
  if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
    left_ = false;
  if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
    right_ = true;
  if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
    right_ = false;
  if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
    const bool virtual_desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
    const auto delta =
        absolute_.input(virtual_desktop, mouse.lLastX, mouse.lLastY,
                        GetSystemMetrics(virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN),
                        GetSystemMetrics(virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN));
    motion_.input(left_, right_, delta.mouse_x, delta.mouse_y);
  } else {
    absolute_.clear();
    motion_.input(left_, right_, mouse.lLastX, mouse.lLastY);
  }
}

} // namespace cupid::desktop
