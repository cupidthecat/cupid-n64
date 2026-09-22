#include "cupid/desktop/input.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>

namespace cupid::desktop {
namespace {

constexpr std::array buttons{
    SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_LEFT_STICK,     SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,     SDL_GAMEPAD_BUTTON_DPAD_LEFT,      SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};
constexpr std::array axes{
    SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

} // namespace

std::optional<host::GamepadButton> gamepad_button(Uint8 button) {
    const auto found = std::find(buttons.begin(), buttons.end(), static_cast<SDL_GamepadButton>(button));
    if (found == buttons.end())
        return std::nullopt;
    return static_cast<host::GamepadButton>(found - buttons.begin());
}

std::optional<host::GamepadAxis> gamepad_axis(Uint8 axis) {
    const auto found = std::find(axes.begin(), axes.end(), static_cast<SDL_GamepadAxis>(axis));
    if (found == axes.end())
        return std::nullopt;
    return static_cast<host::GamepadAxis>(found - axes.begin());
}

InputDevices::InputDevices(host::InputMapper& mapper) : mapper_(mapper) {}

InputDevices::~InputDevices() {
    set_rumble({});
    for (auto& pad : pads_) {
        if (pad.device) {
            mapper_.remove_gamepad(pad.id);
            SDL_CloseGamepad(pad.device);
        }
    }
    if (initialized_)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

bool InputDevices::open(std::string& error) {
    error.clear();
    if (!initialized_ && !SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        error = SDL_GetError();
        return false;
    }
    initialized_ = true;
    scan();
    return true;
}

void InputDevices::scan() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int index = 0; index < count; ++index)
        add(ids[index]);
    SDL_free(ids);
}

void InputDevices::add(SDL_JoystickID id) {
    if (std::any_of(pads_.begin(), pads_.end(), [&](const Pad& pad) { return pad.device && pad.id == id; }))
        return;
    const auto free = std::find_if(pads_.begin(), pads_.end(), [](const Pad& pad) { return !pad.device; });
    if (free == pads_.end())
        return;
    if (auto* device = SDL_OpenGamepad(id)) {
        *free = {device, id};
        static_cast<void>(mapper_.add_gamepad(id));
    }
}

void InputDevices::remove(SDL_JoystickID id) {
    for (auto& pad : pads_) {
        if (pad.device && pad.id == id) {
            mapper_.remove_gamepad(id);
            SDL_CloseGamepad(pad.device);
            pad = {};
        }
    }
    scan();
}

void InputDevices::event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        add(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        remove(event.gdevice.which);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        static_cast<void>(
            mapper_.set_keyboard_scancode(static_cast<int>(event.key.scancode), event.key.down));
        break;
    default:
        break;
    }
}

void InputDevices::poll() {
    if (!initialized_ || !mapper_.focused())
        return;
    SDL_UpdateGamepads();
    std::array<SDL_JoystickID, 4> disconnected{};
    std::size_t disconnected_count = 0;
    for (auto& pad : pads_) {
        if (!pad.device)
            continue;
        if (!SDL_GamepadConnected(pad.device)) {
            disconnected[disconnected_count++] = pad.id;
            continue;
        }
        host::GamepadState state;
        state.instance = pad.id;
        for (std::size_t button = 0; button < buttons.size(); ++button)
            state.buttons[button] = SDL_GetGamepadButton(pad.device, buttons[button]);
        for (std::size_t axis = 0; axis < axes.size(); ++axis)
            state.axes[axis] = SDL_GetGamepadAxis(pad.device, axes[axis]);
        static_cast<void>(mapper_.update_gamepad(state));
    }
    for (std::size_t index = 0; index < disconnected_count; ++index)
        remove(disconnected[index]);
}

void InputDevices::set_focused(bool focused) {
    mapper_.set_focused(focused);
    if (!focused)
        set_rumble({});
}

void InputDevices::set_rumble(const std::array<bool, 4>& enabled) {
    const auto now = SDL_GetTicks();
    for (auto& pad : pads_) {
        if (!pad.device || !pad.rumble_supported)
            continue;
        bool requested = false;
        for (unsigned port = 0; port < enabled.size(); ++port)
            requested |= mapper_.focused() && enabled[port] && mapper_.assigned_gamepad(port) == pad.id;
        if (requested == pad.rumbling && (!requested || now - pad.last_rumble < 75))
            continue;
        const Uint16 strength = requested ? 0xffff : 0;
        pad.rumble_supported = SDL_RumbleGamepad(pad.device, strength, strength, requested ? 150 : 0);
        pad.rumbling = requested;
        pad.last_rumble = now;
    }
}

std::string InputDevices::name(unsigned port) const {
    const auto id = mapper_.assigned_gamepad(port);
    for (const auto& pad : pads_)
        if (pad.device && id == pad.id)
            if (const char* name = SDL_GetGamepadName(pad.device))
                return name;
    return "No gamepad";
}

} // namespace cupid::desktop
