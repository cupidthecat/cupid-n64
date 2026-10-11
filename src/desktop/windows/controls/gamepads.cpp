#include "desktop/windows/controls/gamepads.hpp"
#include <algorithm>
#include <bit>

namespace cupid::desktop {

Gamepads::Gamepads() {
  for (const auto name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
    library_ = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_)
      continue;
    read_ = std::bit_cast<decltype(read_)>(GetProcAddress(library_, "XInputGetState"));
    output_ = std::bit_cast<decltype(output_)>(GetProcAddress(library_, "XInputSetState"));
    capabilities_ =
        std::bit_cast<decltype(capabilities_)>(GetProcAddress(library_, "XInputGetCapabilities"));
    if (read_)
      break;
    FreeLibrary(library_);
    library_ = nullptr;
    output_ = nullptr;
    capabilities_ = nullptr;
  }
}

Gamepads::~Gamepads() {
  stop();
  if (library_)
    FreeLibrary(library_);
}

std::array<HostGamepad, 4> Gamepads::poll(const std::array<bool, 4> &wanted) {
  const auto now = std::chrono::steady_clock::now();
  for (unsigned device = 0; device < pads_.size(); ++device) {
    auto &pad = pads_[device];
    if (!wanted[device] || !read_) {
      if (motors_[device] && output_) {
        XINPUT_VIBRATION silent{};
        output_(device, &silent);
      }
      motors_[device] = false;
      pad = {};
      retry_[device] = {};
      continue;
    }
    if (!pad.connected && now < retry_[device])
      continue;
    XINPUT_STATE state{};
    if (read_(device, &state) != ERROR_SUCCESS) {
      if (motors_[device] && output_) {
        XINPUT_VIBRATION silent{};
        output_(device, &silent);
      }
      motors_[device] = false;
      pad = {};
      retry_[device] = now + std::chrono::milliseconds(500);
      continue;
    }
    if (!pad.connected) {
      XINPUT_CAPABILITIES capabilities{};
      pad.feedback =
          output_ && capabilities_ &&
          capabilities_(device, XINPUT_FLAG_GAMEPAD, &capabilities) == ERROR_SUCCESS &&
          ((capabilities.Flags & XINPUT_CAPS_FFB_SUPPORTED) ||
           capabilities.Vibration.wLeftMotorSpeed || capabilities.Vibration.wRightMotorSpeed);
      if (output_) {
        XINPUT_VIBRATION silent{};
        output_(device, &silent);
      }
    }
    const auto y = [](SHORT axis) {
      return static_cast<std::int16_t>(std::clamp(-int(axis), -32768, 32767));
    };
    pad.connected = true;
    pad.buttons = state.Gamepad.wButtons;
    pad.axes = {state.Gamepad.sThumbLX,
                y(state.Gamepad.sThumbLY),
                state.Gamepad.sThumbRX,
                y(state.Gamepad.sThumbRY),
                static_cast<std::int16_t>(unsigned(state.Gamepad.bLeftTrigger) * 32767 / 255),
                static_cast<std::int16_t>(unsigned(state.Gamepad.bRightTrigger) * 32767 / 255)};
  }
  return pads_;
}

void Gamepads::rumble(const std::array<bool, 4> &motors) {
  if (!output_)
    return;
  for (unsigned device = 0; device < motors.size(); ++device) {
    const bool active = motors[device] && pads_[device].connected && pads_[device].feedback;
    if (active == motors_[device])
      continue;
    XINPUT_VIBRATION vibration{static_cast<WORD>(active ? 0xffff : 0),
                               static_cast<WORD>(active ? 0xffff : 0)};
    if (output_(device, &vibration) == ERROR_SUCCESS)
      motors_[device] = active;
    else {
      motors_[device] = false;
      pads_[device].feedback = false;
    }
  }
}

void Gamepads::stop() {
  if (output_)
    for (unsigned device = 0; device < motors_.size(); ++device) {
      if (!motors_[device])
        continue;
      XINPUT_VIBRATION silent{};
      output_(device, &silent);
    }
  motors_.fill(false);
}

} // namespace cupid::desktop
