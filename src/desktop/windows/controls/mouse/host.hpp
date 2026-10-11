#pragma once

#include "desktop/controls/mouse/absolute.hpp"
#include "desktop/controls/mouse/motion.hpp"
#include <windows.h>

namespace cupid::desktop {

class HostMouse {
public:
  HostMouse() = default;
  HostMouse(const HostMouse &) = delete;
  HostMouse &operator=(const HostMouse &) = delete;
  ~HostMouse() {
    release();
  }
  bool captured() const {
    return window_ != nullptr;
  }
  void acquire(HWND window, RECT client);
  void release();
  void raw_input(HRAWINPUT input);
  PortInput input() {
    return motion_.take();
  }

private:
  void packet(const RAWMOUSE &mouse);
  HWND window_ = nullptr;
  RECT previous_clip_{};
  POINT previous_position_{};
  unsigned hidden_ = 0;
  bool left_ = false, right_ = false;
  MouseMotion motion_;
  AbsoluteMouseMotion absolute_;
};

} // namespace cupid::desktop
