#include "cupid/desktop/input.hpp"
#include "test.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <stdexcept>
#include <string>

using namespace cupid;
using namespace cupid::desktop;
using namespace cupid::host;

namespace {

std::array<ControllerState, 4> configured_states() {
    std::array<ControllerState, 4> states{};
    for (auto& state : states)
        state.connected = true;
    return states;
}

InputBindings gamepad_only_bindings() {
    auto bindings = default_input_bindings();
    for (auto& port : bindings.ports) {
        port.keyboard_enabled = false;
        port.gamepad_enabled = true;
        for (auto& button : port.buttons) {
            button.keyboard_scancode = KeyboardScancodeUnbound;
            button.gamepad_button = GamepadButton::Count;
            button.gamepad_axis = GamepadAxis::Count;
            button.axis_direction = AxisDirection::Positive;
            button.axis_threshold = 16000;
        }
        port.stick.x_axis = GamepadAxis::LeftX;
        port.stick.y_axis = GamepadAxis::LeftY;
        port.stick.invert_x = false;
        port.stick.invert_y = false;
        port.stick.deadzone = 0;
        port.stick.input_range = 32767;
        port.stick.output_range = 80;
    }
    return bindings;
}

[[noreturn]] void sdl_failure(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

class VirtualGamepad {
  public:
    explicit VirtualGamepad(const char* name) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.vendor_id = 0x1209;
        desc.product_id = 0xc064;
        desc.name = name;
        id_ = SDL_AttachVirtualJoystick(&desc);
        if (id_ == 0)
            sdl_failure("SDL_AttachVirtualJoystick");
        joystick_ = SDL_OpenJoystick(id_);
        if (!joystick_)
            sdl_failure("SDL_OpenJoystick");
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
        axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
        SDL_UpdateJoysticks();
    }

    ~VirtualGamepad() {
        close_handle();
        if (id_ != 0)
            SDL_DetachVirtualJoystick(id_);
    }

    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    [[nodiscard]] SDL_JoystickID id() const {
        return id_;
    }

    void add_to(InputDevices& devices) const {
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_ADDED;
        event.gdevice.which = id_;
        devices.event(event);
    }

    void button(SDL_GamepadButton target, bool pressed) {
        if (!SDL_SetJoystickVirtualButton(joystick_, static_cast<int>(target), pressed))
            sdl_failure("SDL_SetJoystickVirtualButton");
    }

    void axis(SDL_GamepadAxis target, Sint16 value) {
        if (!SDL_SetJoystickVirtualAxis(joystick_, static_cast<int>(target), value))
            sdl_failure("SDL_SetJoystickVirtualAxis");
    }

    void disconnect(InputDevices& devices) {
        const SDL_JoystickID removed = detach();

        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_REMOVED;
        event.gdevice.which = removed;
        devices.event(event);
    }

    [[nodiscard]] SDL_JoystickID detach() {
        const SDL_JoystickID removed = id_;
        close_handle();
        if (!SDL_DetachVirtualJoystick(removed))
            sdl_failure("SDL_DetachVirtualJoystick");
        id_ = 0;
        return removed;
    }

  private:
    void close_handle() {
        if (joystick_) {
            SDL_CloseJoystick(joystick_);
            joystick_ = nullptr;
        }
    }

    SDL_JoystickID id_{};
    SDL_Joystick* joystick_{};
};

void open_devices(InputDevices& devices) {
    std::string error;
    if (!devices.open(error))
        throw std::runtime_error("InputDevices::open: " + error);
}

} // namespace

TEST(desktop_input_adapts_sdl_gamepad_controls_to_host_bindings) {
    CHECK_EQ(gamepad_button(static_cast<Uint8>(SDL_GAMEPAD_BUTTON_SOUTH)),
             std::optional<GamepadButton>{GamepadButton::South});
    CHECK_EQ(gamepad_button(static_cast<Uint8>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)),
             std::optional<GamepadButton>{GamepadButton::DpadRight});
    CHECK(!gamepad_button(static_cast<Uint8>(SDL_GAMEPAD_BUTTON_MISC1)).has_value());
    CHECK_EQ(gamepad_axis(static_cast<Uint8>(SDL_GAMEPAD_AXIS_LEFTX)),
             std::optional<GamepadAxis>{GamepadAxis::LeftX});
    CHECK_EQ(gamepad_axis(static_cast<Uint8>(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)),
             std::optional<GamepadAxis>{GamepadAxis::RightTrigger});
    CHECK(!gamepad_axis(static_cast<Uint8>(SDL_GAMEPAD_AXIS_COUNT)).has_value());

    auto bindings = gamepad_only_bindings();
    bindings.ports[0].buttons[static_cast<std::size_t>(N64Button::A)].gamepad_button = GamepadButton::South;
    auto& z = bindings.ports[0].buttons[static_cast<std::size_t>(N64Button::Z)];
    z.gamepad_axis = GamepadAxis::RightTrigger;
    z.axis_threshold = 16000;
    auto& c_left = bindings.ports[0].buttons[static_cast<std::size_t>(N64Button::CLeft)];
    c_left.gamepad_axis = GamepadAxis::LeftX;
    c_left.axis_direction = AxisDirection::Negative;
    c_left.axis_threshold = 16000;

    InputMapper mapper(configured_states(), bindings);
    InputDevices devices(mapper);
    open_devices(devices);
    VirtualGamepad pad("Cupid virtual integration pad");
    pad.add_to(devices);

    CHECK_EQ(mapper.assigned_gamepad(0), std::optional<GamepadInstanceId>{pad.id()});
    CHECK_EQ(devices.name(0), std::string("Cupid virtual integration pad"));

    pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MIN);
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    devices.poll();

    const auto active = mapper.controller_states()[0];
    const u16 expected_buttons = static_cast<u16>(
        n64_button_mask(N64Button::A) | n64_button_mask(N64Button::Z) | n64_button_mask(N64Button::CLeft));
    CHECK_EQ(active.buttons, expected_buttons);
    CHECK_EQ(active.stick_x, -80);
    CHECK_EQ(active.stick_y, 0);

    devices.set_focused(false);
    const auto unfocused = mapper.controller_states()[0];
    CHECK_EQ(unfocused.buttons, 0U);
    CHECK_EQ(unfocused.stick_x, 0);
    CHECK_EQ(unfocused.stick_y, 0);

    devices.poll();
    devices.set_focused(true);
    const auto before_refresh = mapper.controller_states()[0];
    CHECK_EQ(before_refresh.buttons, 0U);
    CHECK_EQ(before_refresh.stick_x, 0);
    devices.poll();
    CHECK_EQ(mapper.controller_states()[0].buttons, expected_buttons);
    CHECK_EQ(mapper.controller_states()[0].stick_x, -80);

    pad.disconnect(devices);
    CHECK(!mapper.assigned_gamepad(0).has_value());
    const auto disconnected = mapper.controller_states()[0];
    CHECK_EQ(disconnected.buttons, 0U);
    CHECK_EQ(disconnected.stick_x, 0);
    CHECK_EQ(disconnected.stick_y, 0);
}

TEST(desktop_input_maps_four_virtual_gamepads_to_independent_ports_and_reuses_a_freed_port) {
    auto bindings = gamepad_only_bindings();
    bindings.ports[0].buttons[static_cast<std::size_t>(N64Button::A)].gamepad_button = GamepadButton::South;
    bindings.ports[1].buttons[static_cast<std::size_t>(N64Button::B)].gamepad_button = GamepadButton::East;
    bindings.ports[2].buttons[static_cast<std::size_t>(N64Button::Start)].gamepad_button =
        GamepadButton::North;
    bindings.ports[3].buttons[static_cast<std::size_t>(N64Button::R)].gamepad_button =
        GamepadButton::RightShoulder;

    InputMapper mapper(configured_states(), bindings);
    InputDevices devices(mapper);
    open_devices(devices);
    VirtualGamepad first("Cupid virtual pad 1");
    VirtualGamepad second("Cupid virtual pad 2");
    VirtualGamepad third("Cupid virtual pad 3");
    VirtualGamepad fourth("Cupid virtual pad 4");
    first.add_to(devices);
    second.add_to(devices);
    third.add_to(devices);
    fourth.add_to(devices);

    CHECK_EQ(mapper.assigned_gamepad(0), std::optional<GamepadInstanceId>{first.id()});
    CHECK_EQ(mapper.assigned_gamepad(1), std::optional<GamepadInstanceId>{second.id()});
    CHECK_EQ(mapper.assigned_gamepad(2), std::optional<GamepadInstanceId>{third.id()});
    CHECK_EQ(mapper.assigned_gamepad(3), std::optional<GamepadInstanceId>{fourth.id()});

    first.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    second.button(SDL_GAMEPAD_BUTTON_EAST, true);
    third.button(SDL_GAMEPAD_BUTTON_NORTH, true);
    fourth.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    devices.poll();

    auto states = mapper.controller_states();
    CHECK_EQ(states[0].buttons, n64_button_mask(N64Button::A));
    CHECK_EQ(states[1].buttons, n64_button_mask(N64Button::B));
    CHECK_EQ(states[2].buttons, n64_button_mask(N64Button::Start));
    CHECK_EQ(states[3].buttons, n64_button_mask(N64Button::R));

    second.disconnect(devices);
    states = mapper.controller_states();
    CHECK_EQ(states[0].buttons, n64_button_mask(N64Button::A));
    CHECK_EQ(states[1].buttons, 0U);
    CHECK_EQ(states[2].buttons, n64_button_mask(N64Button::Start));
    CHECK_EQ(states[3].buttons, n64_button_mask(N64Button::R));

    VirtualGamepad replacement("Cupid virtual replacement pad");
    replacement.add_to(devices);
    CHECK_EQ(mapper.assigned_gamepad(1), std::optional<GamepadInstanceId>{replacement.id()});
    replacement.button(SDL_GAMEPAD_BUTTON_EAST, true);
    devices.poll();
    CHECK_EQ(mapper.controller_states()[1].buttons, n64_button_mask(N64Button::B));
}

TEST(desktop_input_poll_clears_a_disconnected_pad_before_its_removal_event) {
    auto bindings = gamepad_only_bindings();
    bindings.ports[0].buttons[static_cast<std::size_t>(N64Button::A)].gamepad_button = GamepadButton::South;

    InputMapper mapper(configured_states(), bindings);
    InputDevices devices(mapper);
    open_devices(devices);
    VirtualGamepad pad("Cupid virtual unplugged pad");
    pad.add_to(devices);
    const auto assigned = pad.id();

    pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    devices.poll();
    CHECK_EQ(mapper.controller_states()[0].buttons, n64_button_mask(N64Button::A));

    CHECK_EQ(pad.detach(), assigned);
    CHECK_EQ(mapper.assigned_gamepad(0), std::optional<GamepadInstanceId>{assigned});
    devices.poll();
    CHECK(!mapper.assigned_gamepad(0).has_value());
    CHECK_EQ(mapper.controller_states()[0].buttons, 0U);
    CHECK_EQ(mapper.controller_states()[0].stick_x, 0);
    CHECK_EQ(mapper.controller_states()[0].stick_y, 0);
}
