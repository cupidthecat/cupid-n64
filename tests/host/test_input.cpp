#include "cupid/host/input.hpp"
#include "test.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {

using namespace cupid;
using namespace cupid::host;

std::array<ControllerState, 4> configured_states() {
    std::array<ControllerState, 4> states{};
    for (auto& state : states) {
        state.connected = true;
        state.accessory = ControllerAccessory::ControllerPak;
        state.device = ControllerDevice::Gamepad;
    }
    states[0].accessory = ControllerAccessory::TransferPak;
    states[1].device = ControllerDevice::Mouse;
    states[2].connected = false;
    return states;
}

void press(GamepadState& state, GamepadButton button) {
    state.buttons[static_cast<std::size_t>(button)] = true;
}

void axis(GamepadState& state, GamepadAxis target, s16 value) {
    state.axes[static_cast<std::size_t>(target)] = value;
}

} // namespace

TEST(host_input_keyboard_defaults_cover_every_n64_button_and_preserve_hardware_configuration) {
    InputMapper mapper(configured_states());
    const std::array<int, 14> keys{27, 29, 40, 225, 20, 8, 82, 81, 80, 79, 12, 14, 13, 15};
    for (const int key : keys)
        CHECK(mapper.set_keyboard_scancode(key, true));

    const auto states = mapper.controller_states();
    CHECK_EQ(states[0].buttons, 0xff3fU);
    CHECK(states[0].connected);
    CHECK_EQ(states[0].accessory, ControllerAccessory::TransferPak);
    CHECK_EQ(states[0].device, ControllerDevice::Gamepad);
    CHECK_EQ(states[1].buttons, 0U);
    CHECK_EQ(states[1].device, ControllerDevice::Mouse);
    CHECK(!states[2].connected);
    CHECK_EQ(states[2].buttons, 0U);

    CHECK(!mapper.set_keyboard_scancode(-1, true));
    CHECK(!mapper.set_keyboard_scancode(512, true));
    CHECK(mapper.set_keyboard_scancode(0, true));
    CHECK(mapper.set_keyboard_scancode(511, true));
}

TEST(host_input_xbox_defaults_map_buttons_trigger_and_right_stick_c_directions) {
    InputMapper mapper(configured_states());
    CHECK_EQ(mapper.add_gamepad(101), std::optional<unsigned>{0});

    GamepadState pad{};
    pad.instance = 101;
    press(pad, GamepadButton::South);
    press(pad, GamepadButton::West);
    press(pad, GamepadButton::Start);
    press(pad, GamepadButton::LeftShoulder);
    press(pad, GamepadButton::RightShoulder);
    press(pad, GamepadButton::DpadUp);
    press(pad, GamepadButton::DpadRight);
    axis(pad, GamepadAxis::LeftTrigger, 20000);
    axis(pad, GamepadAxis::RightX, 20000);
    axis(pad, GamepadAxis::RightY, -20000);
    CHECK(mapper.update_gamepad(pad));

    CHECK_EQ(mapper.controller_states()[0].buttons, 0xf939U);

    axis(pad, GamepadAxis::RightX, -20000);
    axis(pad, GamepadAxis::RightY, 20000);
    CHECK(mapper.update_gamepad(pad));
    const u16 c_mask =
        static_cast<u16>(n64_button_mask(N64Button::CDown) | n64_button_mask(N64Button::CLeft));
    CHECK_EQ(mapper.controller_states()[0].buttons & 0x000fU, c_mask);
}

TEST(host_input_stick_deadzone_range_inversion_and_diagonal_are_deterministic) {
    InputMapper mapper(configured_states());
    CHECK(mapper.add_gamepad(7).has_value());
    GamepadState pad{};
    pad.instance = 7;

    axis(pad, GamepadAxis::LeftX, 4096);
    CHECK(mapper.update_gamepad(pad));
    CHECK_EQ(mapper.controller_states()[0].stick_x, 0);

    axis(pad, GamepadAxis::LeftX, 32767);
    CHECK(mapper.update_gamepad(pad));
    CHECK_EQ(mapper.controller_states()[0].stick_x, 80);
    CHECK_EQ(mapper.controller_states()[0].stick_y, 0);

    axis(pad, GamepadAxis::LeftY, -32767);
    CHECK(mapper.update_gamepad(pad));
    const auto diagonal = mapper.controller_states()[0];
    CHECK_EQ(diagonal.stick_x, 56);
    CHECK_EQ(diagonal.stick_y, 56);
    const int diagonal_square = static_cast<int>(diagonal.stick_x) * diagonal.stick_x +
                                static_cast<int>(diagonal.stick_y) * diagonal.stick_y;
    CHECK(diagonal_square <= 80 * 80);

    auto bindings = mapper.bindings();
    bindings.ports[0].stick.invert_x = true;
    bindings.ports[0].stick.invert_y = false;
    bindings.ports[0].stick.deadzone = 1000;
    bindings.ports[0].stick.input_range = 20000;
    bindings.ports[0].stick.output_range = 100;
    std::string error;
    CHECK(mapper.set_bindings(bindings, error));
    axis(pad, GamepadAxis::LeftX, 20000);
    axis(pad, GamepadAxis::LeftY, 0);
    CHECK(mapper.update_gamepad(pad));
    CHECK_EQ(mapper.controller_states()[0].stick_x, -100);
}

TEST(host_input_keyboard_diagonal_is_bounded_and_opposing_keys_cancel) {
    InputMapper mapper(configured_states());
    CHECK(mapper.set_keyboard_scancode(7, true));
    CHECK(mapper.set_keyboard_scancode(26, true));
    const auto diagonal = mapper.controller_states()[0];
    CHECK_EQ(diagonal.stick_x, 56);
    CHECK_EQ(diagonal.stick_y, 56);
    CHECK(static_cast<int>(diagonal.stick_x) * diagonal.stick_x +
              static_cast<int>(diagonal.stick_y) * diagonal.stick_y <=
          80 * 80);

    CHECK(mapper.set_keyboard_scancode(4, true));
    CHECK(mapper.set_keyboard_scancode(22, true));
    const auto cancelled = mapper.controller_states()[0];
    CHECK_EQ(cancelled.stick_x, 0);
    CHECK_EQ(cancelled.stick_y, 0);
}

TEST(host_input_hotplug_assignment_is_isolated_and_removed_devices_release_immediately) {
    auto bindings = default_input_bindings();
    bindings.ports[0].keyboard_enabled = false;
    InputMapper mapper(configured_states(), bindings);
    CHECK_EQ(mapper.add_gamepad(11), std::optional<unsigned>{0});
    CHECK_EQ(mapper.add_gamepad(22), std::optional<unsigned>{1});
    CHECK_EQ(mapper.assigned_gamepad(0), std::optional<GamepadInstanceId>{11});
    CHECK_EQ(mapper.assigned_gamepad(1), std::optional<GamepadInstanceId>{22});

    GamepadState first{};
    first.instance = 11;
    press(first, GamepadButton::South);
    GamepadState second{};
    second.instance = 22;
    press(second, GamepadButton::West);
    CHECK(mapper.update_gamepad(first));
    CHECK(mapper.update_gamepad(second));
    auto states = mapper.controller_states();
    CHECK_EQ(states[0].buttons, n64_button_mask(N64Button::A));
    CHECK_EQ(states[1].buttons, n64_button_mask(N64Button::B));

    mapper.remove_gamepad(11);
    states = mapper.controller_states();
    CHECK_EQ(states[0].buttons, 0U);
    CHECK_EQ(states[1].buttons, n64_button_mask(N64Button::B));
    CHECK_EQ(mapper.assigned_gamepad(1), std::optional<GamepadInstanceId>{22});

    CHECK_EQ(mapper.add_gamepad(33), std::optional<unsigned>{0});
    CHECK(!mapper.update_gamepad(first));
    GamepadState replacement{};
    replacement.instance = 33;
    press(replacement, GamepadButton::Start);
    CHECK(mapper.update_gamepad(replacement));
    states = mapper.controller_states();
    CHECK_EQ(states[0].buttons, n64_button_mask(N64Button::Start));
    CHECK_EQ(states[1].buttons, n64_button_mask(N64Button::B));
}

TEST(host_input_unassigned_connected_pad_claims_port_when_hotplug_removal_frees_it) {
    auto bindings = default_input_bindings();
    bindings.ports[0].keyboard_enabled = false;
    bindings.ports[3].gamepad_enabled = false;
    InputMapper mapper(configured_states(), bindings);
    CHECK_EQ(mapper.add_gamepad(10), std::optional<unsigned>{0});
    CHECK_EQ(mapper.add_gamepad(20), std::optional<unsigned>{1});
    CHECK_EQ(mapper.add_gamepad(30), std::optional<unsigned>{2});
    CHECK(!mapper.add_gamepad(40).has_value());
    CHECK(!mapper.assigned_gamepad(3).has_value());

    mapper.remove_gamepad(20);
    CHECK_EQ(mapper.assigned_gamepad(1), std::optional<GamepadInstanceId>{40});
    CHECK(!mapper.add_gamepad(50).has_value());
}

TEST(host_input_focus_loss_releases_keyboard_and_gamepad_until_fresh_input_arrives) {
    InputMapper mapper(configured_states());
    CHECK(mapper.add_gamepad(99).has_value());
    CHECK(mapper.set_keyboard_scancode(27, true));
    GamepadState pad{};
    pad.instance = 99;
    press(pad, GamepadButton::West);
    axis(pad, GamepadAxis::LeftX, 32767);
    CHECK(mapper.update_gamepad(pad));
    CHECK_EQ(mapper.controller_states()[0].buttons,
             static_cast<u16>(n64_button_mask(N64Button::A) | n64_button_mask(N64Button::B)));
    CHECK_EQ(mapper.controller_states()[0].stick_x, 80);

    mapper.set_focused(false);
    auto released = mapper.controller_states()[0];
    CHECK_EQ(released.buttons, 0U);
    CHECK_EQ(released.stick_x, 0);
    CHECK(mapper.set_keyboard_scancode(27, true));
    CHECK(mapper.update_gamepad(pad));
    mapper.set_focused(true);
    released = mapper.controller_states()[0];
    CHECK_EQ(released.buttons, 0U);
    CHECK_EQ(released.stick_x, 0);

    CHECK(mapper.set_keyboard_scancode(27, true));
    CHECK(mapper.update_gamepad(pad));
    CHECK_EQ(mapper.controller_states()[0].buttons,
             static_cast<u16>(n64_button_mask(N64Button::A) | n64_button_mask(N64Button::B)));
}

TEST(host_input_binding_changes_reassign_connected_pads_without_cross_port_state) {
    auto bindings = default_input_bindings();
    bindings.ports[0].keyboard_enabled = false;
    InputMapper mapper(configured_states(), bindings);
    CHECK_EQ(mapper.add_gamepad(1), std::optional<unsigned>{0});
    CHECK_EQ(mapper.add_gamepad(2), std::optional<unsigned>{1});

    bindings.ports[0].gamepad_enabled = false;
    std::string error;
    CHECK(mapper.set_bindings(bindings, error));
    CHECK(!mapper.assigned_gamepad(0).has_value());
    CHECK_EQ(mapper.assigned_gamepad(1), std::optional<GamepadInstanceId>{2});
    CHECK_EQ(mapper.assigned_gamepad(2), std::optional<GamepadInstanceId>{1});
}

TEST(host_input_settings_roundtrip_and_malformed_decode_is_atomic) {
    auto source = default_input_bindings();
    source.ports[3].keyboard_enabled = true;
    source.ports[3].stick.left_key = 511;
    source.ports[3].stick.invert_x = true;
    source.ports[3].stick.deadzone = 1234;
    source.ports[3].stick.input_range = 30000;
    source.ports[3].stick.output_range = 101;
    source.ports[2].buttons[static_cast<std::size_t>(N64Button::Z)].keyboard_scancode = 0;
    source.ports[2].buttons[static_cast<std::size_t>(N64Button::Z)].axis_threshold = 23456;

    const std::string encoded = encode_input_bindings(source);
    CHECK(encoded.size() <= MaxInputSettingsTextBytes);
    InputBindings decoded;
    std::string error;
    CHECK(decode_input_bindings(encoded, decoded, error));
    CHECK_EQ(encode_input_bindings(decoded), encoded);

    auto destination = default_input_bindings();
    destination.ports[0].stick.output_range = 42;
    const std::string before = encode_input_bindings(destination);

    const auto newline = encoded.find('\n');
    const std::string wrong_version = "CUPID_INPUT 2\n" + encoded.substr(newline + 1U);
    CHECK(!decode_input_bindings(wrong_version, destination, error));
    CHECK_EQ(encode_input_bindings(destination), before);

    std::string bad_scancode = encoded;
    const auto binding = bad_scancode.find("BUTTON 0 0 27 ");
    CHECK(binding != std::string::npos);
    bad_scancode.replace(binding, std::string("BUTTON 0 0 27 ").size(), "BUTTON 0 0 999 ");
    CHECK(!decode_input_bindings(bad_scancode, destination, error));
    CHECK_EQ(encode_input_bindings(destination), before);

    CHECK(!decode_input_bindings(encoded.substr(0, encoded.size() / 2U), destination, error));
    CHECK_EQ(encode_input_bindings(destination), before);
    CHECK(!decode_input_bindings(encoded + "junk\n", destination, error));
    CHECK_EQ(encode_input_bindings(destination), before);
    CHECK(!decode_input_bindings(std::string(MaxInputSettingsTextBytes + 1U, 'x'), destination, error));
    CHECK_EQ(encode_input_bindings(destination), before);

    std::string wrapped_enum = encoded;
    const auto first_button = wrapped_enum.find("BUTTON 0 0 27 0 6 1 16000");
    CHECK(first_button != std::string::npos);
    wrapped_enum.replace(first_button, std::string("BUTTON 0 0 27 0 6 1 16000").size(),
                         "BUTTON 0 0 27 256 6 1 16000");
    CHECK(!decode_input_bindings(wrapped_enum, destination, error));
    CHECK_EQ(encode_input_bindings(destination), before);
}

TEST(host_input_invalid_rebind_is_rejected_without_changing_live_bindings) {
    InputMapper mapper(configured_states());
    const std::string before = encode_input_bindings(mapper.bindings());
    auto invalid = mapper.bindings();
    invalid.ports[0].stick.deadzone = invalid.ports[0].stick.input_range;
    std::string error;
    CHECK(!mapper.set_bindings(invalid, error));
    CHECK_EQ(encode_input_bindings(mapper.bindings()), before);
}
