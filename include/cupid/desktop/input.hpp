#pragma once

#include "cupid/host/input.hpp"

#include <SDL3/SDL_events.h>

#include <array>
#include <optional>
#include <string>

struct SDL_Gamepad;

namespace cupid::desktop {

[[nodiscard]] std::optional<host::GamepadButton> gamepad_button(Uint8 button);
[[nodiscard]] std::optional<host::GamepadAxis> gamepad_axis(Uint8 axis);

class InputDevices {
  public:
    explicit InputDevices(host::InputMapper& mapper);
    ~InputDevices();
    InputDevices(const InputDevices&) = delete;
    InputDevices& operator=(const InputDevices&) = delete;

    bool open(std::string& error);
    void event(const SDL_Event& event);
    void poll();
    void set_focused(bool focused);
    void set_rumble(const std::array<bool, 4>& enabled);
    [[nodiscard]] std::string name(unsigned port) const;

  private:
    struct Pad {
        SDL_Gamepad* device{};
        SDL_JoystickID id{};
        Uint64 last_rumble{};
        bool rumbling{};
        bool rumble_supported{true};
    };

    void scan();
    void add(SDL_JoystickID id);
    void remove(SDL_JoystickID id);

    host::InputMapper& mapper_;
    std::array<Pad, 4> pads_{};
    bool initialized_{};
};

} // namespace cupid::desktop
