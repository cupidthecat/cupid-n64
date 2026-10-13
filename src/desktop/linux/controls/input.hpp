#pragma once

#include "desktop/controls/mapping.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <unordered_map>

namespace cupid::desktop::linux_host {

unsigned key_code(unsigned keyval);

class Input {
public:
  Input();
  ~Input();
  Input(const Input &) = delete;
  Input &operator=(const Input &) = delete;
  void key(unsigned keyval, bool pressed, unsigned hardware_code);
  void clear();
  HostInputs poll();
  void rumble(const std::array<bool, 4> &motors);
  void stop();

private:
  void enumerate();
  HostInputs host_;
  std::unordered_map<unsigned, unsigned> pressed_keys_;
  std::array<SDL_Gamepad *, 4> controllers_{};
  std::array<bool, 4> motors_{};
  std::chrono::steady_clock::time_point renewed_{};
  bool initialized_ = false;
};

} // namespace cupid::desktop::linux_host
