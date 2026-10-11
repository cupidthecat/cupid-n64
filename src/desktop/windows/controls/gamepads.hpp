#pragma once

#include "desktop/controls/mapping.hpp"
#include <chrono>
#include <windows.h>
#include <xinput.h>

namespace cupid::desktop {

class Gamepads {
public:
  Gamepads();
  ~Gamepads();
  Gamepads(const Gamepads &) = delete;
  Gamepads &operator=(const Gamepads &) = delete;
  std::array<HostGamepad, 4> poll(const std::array<bool, 4> &wanted);
  void rumble(const std::array<bool, 4> &motors);
  void stop();
  bool available() const {
    return read_ != nullptr;
  }

private:
  HMODULE library_ = nullptr;
  decltype(&XInputGetState) read_ = nullptr;
  decltype(&XInputGetCapabilities) capabilities_ = nullptr;
  decltype(&XInputSetState) output_ = nullptr;
  std::array<HostGamepad, 4> pads_{};
  std::array<bool, 4> motors_{};
  std::array<std::chrono::steady_clock::time_point, 4> retry_{};
};

} // namespace cupid::desktop
