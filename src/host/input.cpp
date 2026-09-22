#include "cupid/host/input.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <utility>

namespace cupid::host {
namespace {

constexpr s32 AxisExtent = 32767;

constexpr std::size_t button_index(N64Button button) {
    return static_cast<std::size_t>(button);
}

constexpr std::size_t gamepad_button_index(GamepadButton button) {
    return static_cast<std::size_t>(button);
}

constexpr std::size_t gamepad_axis_index(GamepadAxis axis) {
    return static_cast<std::size_t>(axis);
}

bool valid_binding_scancode(int scancode) {
    return scancode == KeyboardScancodeUnbound || valid_keyboard_scancode(scancode);
}

bool valid_gamepad_button(GamepadButton button) {
    return static_cast<unsigned>(button) <= static_cast<unsigned>(GamepadButton::Count);
}

bool valid_gamepad_axis(GamepadAxis axis) {
    return static_cast<unsigned>(axis) <= static_cast<unsigned>(GamepadAxis::Count);
}

bool valid_direction(AxisDirection direction) {
    return direction == AxisDirection::Negative || direction == AxisDirection::Positive;
}

void set_default_button(PortInputBindings& port, N64Button target, int key,
                        GamepadButton button = GamepadButton::Count, GamepadAxis axis = GamepadAxis::Count,
                        AxisDirection direction = AxisDirection::Positive, u16 threshold = 16000) {
    auto& binding = port.buttons[button_index(target)];
    binding.keyboard_scancode = key;
    binding.gamepad_button = button;
    binding.gamepad_axis = axis;
    binding.axis_direction = direction;
    binding.axis_threshold = threshold;
}

s32 normalized_axis(s16 value) {
    return std::max<s32>(-AxisExtent, static_cast<s32>(value));
}

s32 choose_stronger(s32 first, s32 second) {
    const auto first_magnitude = first < 0 ? -first : first;
    const auto second_magnitude = second < 0 ? -second : second;
    return second_magnitude > first_magnitude ? second : first;
}

u64 integer_sqrt(u64 value) {
    u64 result = 0;
    u64 bit = 1ULL << 62U;
    while (bit > value)
        bit >>= 2U;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result;
}

std::pair<s8, s8> map_stick(s32 raw_x, s32 raw_y, const StickBinding& binding) {
    const u64 square_x = static_cast<u64>(static_cast<s64>(raw_x) * raw_x);
    const u64 square_y = static_cast<u64>(static_cast<s64>(raw_y) * raw_y);
    const u64 magnitude = integer_sqrt(square_x + square_y);
    if (magnitude <= binding.deadzone || magnitude == 0)
        return {};

    const u64 clamped = std::min<u64>(magnitude, binding.input_range);
    const u64 span = static_cast<u64>(binding.input_range - binding.deadzone);
    const u64 scaled_radius = ((clamped - binding.deadzone) * binding.output_range) / span;
    const s64 x = (static_cast<s64>(raw_x) * static_cast<s64>(scaled_radius)) / static_cast<s64>(magnitude);
    const s64 y = (static_cast<s64>(raw_y) * static_cast<s64>(scaled_radius)) / static_cast<s64>(magnitude);
    return {static_cast<s8>(std::clamp<s64>(x, -127, 127)), static_cast<s8>(std::clamp<s64>(y, -127, 127))};
}

bool digital_active(const DigitalBinding& binding, bool keyboard_enabled,
                    const std::array<bool, static_cast<std::size_t>(KeyboardScancodeMax + 1)>& keys,
                    bool gamepad_enabled, const GamepadState* pad) {
    if (keyboard_enabled && valid_keyboard_scancode(binding.keyboard_scancode) &&
        keys[static_cast<std::size_t>(binding.keyboard_scancode)])
        return true;
    if (!gamepad_enabled || pad == nullptr)
        return false;
    if (binding.gamepad_button != GamepadButton::Count &&
        pad->buttons[gamepad_button_index(binding.gamepad_button)])
        return true;
    if (binding.gamepad_axis == GamepadAxis::Count)
        return false;
    const s32 value = normalized_axis(pad->axes[gamepad_axis_index(binding.gamepad_axis)]);
    const s32 threshold = static_cast<s32>(binding.axis_threshold);
    return binding.axis_direction == AxisDirection::Positive ? value >= threshold : value <= -threshold;
}

bool parse_integer(std::istringstream& input, int& value) {
    input >> value;
    return static_cast<bool>(input);
}

bool parse_label(std::istringstream& input, std::string_view expected) {
    std::string token;
    input >> token;
    return input && token == expected;
}

std::string settings_error(std::string_view detail) {
    return "Invalid input settings: " + std::string(detail);
}

} // namespace

bool valid_keyboard_scancode(int scancode) {
    return scancode >= KeyboardScancodeMin && scancode <= KeyboardScancodeMax;
}

bool validate_input_bindings(const InputBindings& bindings, std::string& error) {
    for (std::size_t port_index = 0; port_index < bindings.ports.size(); ++port_index) {
        const auto& port = bindings.ports[port_index];
        const auto& stick = port.stick;
        if (!valid_binding_scancode(stick.left_key) || !valid_binding_scancode(stick.right_key) ||
            !valid_binding_scancode(stick.up_key) || !valid_binding_scancode(stick.down_key)) {
            error =
                "Port " + std::to_string(port_index + 1U) + " has a keyboard scancode outside -1 or 0..511.";
            return false;
        }
        if (stick.x_axis == GamepadAxis::Count || stick.y_axis == GamepadAxis::Count ||
            !valid_gamepad_axis(stick.x_axis) || !valid_gamepad_axis(stick.y_axis)) {
            error = "Port " + std::to_string(port_index + 1U) + " has an invalid analog stick axis.";
            return false;
        }
        if (stick.input_range == 0 || stick.input_range > AxisExtent || stick.deadzone >= stick.input_range) {
            error = "Port " + std::to_string(port_index + 1U) +
                    " must use an input range from 1..32767 that is greater than its deadzone.";
            return false;
        }
        if (stick.output_range == 0 || stick.output_range > 127) {
            error = "Port " + std::to_string(port_index + 1U) + " must use an output range from 1..127.";
            return false;
        }
        for (std::size_t button = 0; button < port.buttons.size(); ++button) {
            const auto& binding = port.buttons[button];
            if (!valid_binding_scancode(binding.keyboard_scancode)) {
                error = "Port " + std::to_string(port_index + 1U) + " button " + std::to_string(button) +
                        " has a keyboard scancode outside -1 or 0..511.";
                return false;
            }
            if (!valid_gamepad_button(binding.gamepad_button) || !valid_gamepad_axis(binding.gamepad_axis) ||
                !valid_direction(binding.axis_direction)) {
                error = "Port " + std::to_string(port_index + 1U) + " button " + std::to_string(button) +
                        " has an invalid gamepad binding.";
                return false;
            }
            if (binding.axis_threshold == 0 || binding.axis_threshold > AxisExtent) {
                error = "Port " + std::to_string(port_index + 1U) + " button " + std::to_string(button) +
                        " has an axis threshold outside 1..32767.";
                return false;
            }
        }
    }
    error.clear();
    return true;
}

InputBindings default_input_bindings() {
    InputBindings bindings;
    for (std::size_t index = 0; index < bindings.ports.size(); ++index) {
        auto& port = bindings.ports[index];
        port.keyboard_enabled = index == 0;
        port.gamepad_enabled = true;
        port.stick.left_key = 4;  // A
        port.stick.right_key = 7; // D
        port.stick.up_key = 26;   // W
        port.stick.down_key = 22; // S
        port.stick.x_axis = GamepadAxis::LeftX;
        port.stick.y_axis = GamepadAxis::LeftY;
        port.stick.invert_y = true;
        port.stick.deadzone = 4096;
        port.stick.input_range = 32767;
        port.stick.output_range = 80;

        set_default_button(port, N64Button::A, 27, GamepadButton::South);     // X / A
        set_default_button(port, N64Button::B, 29, GamepadButton::West);      // Z / X
        set_default_button(port, N64Button::Start, 40, GamepadButton::Start); // Enter / Menu
        set_default_button(port, N64Button::Z, 225, GamepadButton::Count, GamepadAxis::LeftTrigger,
                           AxisDirection::Positive, 12000);                      // LShift / LT
        set_default_button(port, N64Button::L, 20, GamepadButton::LeftShoulder); // Q / LB
        set_default_button(port, N64Button::R, 8, GamepadButton::RightShoulder); // E / RB
        set_default_button(port, N64Button::DpadUp, 82, GamepadButton::DpadUp);
        set_default_button(port, N64Button::DpadDown, 81, GamepadButton::DpadDown);
        set_default_button(port, N64Button::DpadLeft, 80, GamepadButton::DpadLeft);
        set_default_button(port, N64Button::DpadRight, 79, GamepadButton::DpadRight);
        set_default_button(port, N64Button::CUp, 12, GamepadButton::Count, GamepadAxis::RightY,
                           AxisDirection::Negative);
        set_default_button(port, N64Button::CDown, 14, GamepadButton::Count, GamepadAxis::RightY,
                           AxisDirection::Positive);
        set_default_button(port, N64Button::CLeft, 13, GamepadButton::Count, GamepadAxis::RightX,
                           AxisDirection::Negative);
        set_default_button(port, N64Button::CRight, 15, GamepadButton::Count, GamepadAxis::RightX,
                           AxisDirection::Positive);
    }
    return bindings;
}

std::string encode_input_bindings(const InputBindings& bindings) {
    std::ostringstream output;
    output << "CUPID_INPUT 1\n";
    for (std::size_t port_index = 0; port_index < bindings.ports.size(); ++port_index) {
        const auto& port = bindings.ports[port_index];
        output << "PORT " << port_index << ' ' << (port.keyboard_enabled ? 1 : 0) << ' '
               << (port.gamepad_enabled ? 1 : 0) << '\n';
        const auto& stick = port.stick;
        output << "STICK " << port_index << ' ' << stick.left_key << ' ' << stick.right_key << ' '
               << stick.up_key << ' ' << stick.down_key << ' ' << static_cast<unsigned>(stick.x_axis) << ' '
               << static_cast<unsigned>(stick.y_axis) << ' ' << (stick.invert_x ? 1 : 0) << ' '
               << (stick.invert_y ? 1 : 0) << ' ' << stick.deadzone << ' ' << stick.input_range << ' '
               << static_cast<unsigned>(stick.output_range) << '\n';
        for (std::size_t button = 0; button < port.buttons.size(); ++button) {
            const auto& binding = port.buttons[button];
            output << "BUTTON " << port_index << ' ' << button << ' ' << binding.keyboard_scancode << ' '
                   << static_cast<unsigned>(binding.gamepad_button) << ' '
                   << static_cast<unsigned>(binding.gamepad_axis) << ' '
                   << static_cast<int>(binding.axis_direction) << ' ' << binding.axis_threshold << '\n';
        }
    }
    output << "END\n";
    return output.str();
}

bool decode_input_bindings(std::string_view text, InputBindings& bindings, std::string& error) {
    if (text.size() > MaxInputSettingsTextBytes) {
        error = settings_error("text exceeds the 16 KiB limit.");
        return false;
    }

    std::istringstream input{std::string(text)};
    int version = 0;
    if (!parse_label(input, "CUPID_INPUT") || !parse_integer(input, version) || version != 1) {
        error = settings_error("missing or unsupported version header.");
        return false;
    }

    InputBindings parsed;
    for (std::size_t port_index = 0; port_index < parsed.ports.size(); ++port_index) {
        int encoded_port = -1;
        int keyboard_enabled = 0;
        int gamepad_enabled = 0;
        if (!parse_label(input, "PORT") || !parse_integer(input, encoded_port) ||
            !parse_integer(input, keyboard_enabled) || !parse_integer(input, gamepad_enabled) ||
            encoded_port != static_cast<int>(port_index) ||
            (keyboard_enabled != 0 && keyboard_enabled != 1) ||
            (gamepad_enabled != 0 && gamepad_enabled != 1)) {
            error = settings_error("malformed port record.");
            return false;
        }
        auto& port = parsed.ports[port_index];
        port.keyboard_enabled = keyboard_enabled != 0;
        port.gamepad_enabled = gamepad_enabled != 0;

        int left_key = 0;
        int right_key = 0;
        int up_key = 0;
        int down_key = 0;
        int x_axis = 0;
        int y_axis = 0;
        int invert_x = 0;
        int invert_y = 0;
        int deadzone = 0;
        int input_range = 0;
        int output_range = 0;
        if (!parse_label(input, "STICK") || !parse_integer(input, encoded_port) ||
            !parse_integer(input, left_key) || !parse_integer(input, right_key) ||
            !parse_integer(input, up_key) || !parse_integer(input, down_key) ||
            !parse_integer(input, x_axis) || !parse_integer(input, y_axis) ||
            !parse_integer(input, invert_x) || !parse_integer(input, invert_y) ||
            !parse_integer(input, deadzone) || !parse_integer(input, input_range) ||
            !parse_integer(input, output_range) || encoded_port != static_cast<int>(port_index) ||
            (invert_x != 0 && invert_x != 1) || (invert_y != 0 && invert_y != 1) || x_axis < 0 ||
            x_axis >= static_cast<int>(GamepadAxis::Count) || y_axis < 0 ||
            y_axis >= static_cast<int>(GamepadAxis::Count) || deadzone < 0 || deadzone > AxisExtent ||
            input_range < 0 || input_range > AxisExtent || output_range < 0 || output_range > 127) {
            error = settings_error("malformed stick record.");
            return false;
        }
        auto& stick = port.stick;
        stick.left_key = left_key;
        stick.right_key = right_key;
        stick.up_key = up_key;
        stick.down_key = down_key;
        stick.x_axis = static_cast<GamepadAxis>(x_axis);
        stick.y_axis = static_cast<GamepadAxis>(y_axis);
        stick.invert_x = invert_x != 0;
        stick.invert_y = invert_y != 0;
        stick.deadzone = static_cast<u16>(deadzone);
        stick.input_range = static_cast<u16>(input_range);
        stick.output_range = static_cast<u8>(output_range);

        for (std::size_t button_index_value = 0; button_index_value < port.buttons.size();
             ++button_index_value) {
            int encoded_button = -1;
            int key = 0;
            int gamepad_button = 0;
            int gamepad_axis = 0;
            int direction = 0;
            int threshold = 0;
            if (!parse_label(input, "BUTTON") || !parse_integer(input, encoded_port) ||
                !parse_integer(input, encoded_button) || !parse_integer(input, key) ||
                !parse_integer(input, gamepad_button) || !parse_integer(input, gamepad_axis) ||
                !parse_integer(input, direction) || !parse_integer(input, threshold) ||
                encoded_port != static_cast<int>(port_index) ||
                encoded_button != static_cast<int>(button_index_value) || gamepad_button < 0 ||
                gamepad_button > static_cast<int>(GamepadButton::Count) || gamepad_axis < 0 ||
                gamepad_axis > static_cast<int>(GamepadAxis::Count) ||
                (direction != static_cast<int>(AxisDirection::Negative) &&
                 direction != static_cast<int>(AxisDirection::Positive)) ||
                threshold <= 0 || threshold > AxisExtent) {
                error = settings_error("malformed button record.");
                return false;
            }
            auto& binding = port.buttons[button_index_value];
            binding.keyboard_scancode = key;
            binding.gamepad_button = static_cast<GamepadButton>(gamepad_button);
            binding.gamepad_axis = static_cast<GamepadAxis>(gamepad_axis);
            binding.axis_direction = static_cast<AxisDirection>(direction);
            binding.axis_threshold = static_cast<u16>(threshold);
        }
    }

    if (!parse_label(input, "END")) {
        error = settings_error("missing end marker.");
        return false;
    }
    std::string trailing;
    if (input >> trailing) {
        error = settings_error("unexpected trailing data.");
        return false;
    }
    if (!validate_input_bindings(parsed, error))
        return false;
    bindings = parsed;
    error.clear();
    return true;
}

InputMapper::InputMapper(std::array<ControllerState, 4> configured, InputBindings bindings)
    : configured_(configured), bindings_(std::move(bindings)) {
    std::string error;
    if (!validate_input_bindings(bindings_, error))
        bindings_ = default_input_bindings();
}

bool InputMapper::set_bindings(const InputBindings& bindings, std::string& error) {
    if (!validate_input_bindings(bindings, error))
        return false;
    bindings_ = bindings;
    for (auto& pad : pads_) {
        if (pad.present && pad.port.has_value() && !bindings_.ports[*pad.port].gamepad_enabled)
            pad.port.reset();
    }
    for (std::size_t slot = 0; slot < pads_.size(); ++slot) {
        if (pads_[slot].present && !pads_[slot].port.has_value())
            static_cast<void>(assign_first_free_port(slot));
    }
    error.clear();
    return true;
}

void InputMapper::set_configured_state(unsigned port, ControllerState state) {
    if (port < configured_.size())
        configured_[port] = state;
}

bool InputMapper::set_keyboard_scancode(int scancode, bool pressed) {
    if (!valid_keyboard_scancode(scancode))
        return false;
    keys_[static_cast<std::size_t>(scancode)] = focused_ && pressed;
    return true;
}

std::optional<std::size_t> InputMapper::find_pad(GamepadInstanceId instance) const {
    for (std::size_t slot = 0; slot < pads_.size(); ++slot) {
        if (pads_[slot].present && pads_[slot].instance == instance)
            return slot;
    }
    return std::nullopt;
}

std::optional<unsigned> InputMapper::assign_first_free_port(std::size_t slot) {
    for (unsigned port = 0; port < bindings_.ports.size(); ++port) {
        if (!bindings_.ports[port].gamepad_enabled)
            continue;
        bool occupied = false;
        for (const auto& pad : pads_) {
            if (pad.present && pad.port == port) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            pads_[slot].port = port;
            return port;
        }
    }
    return std::nullopt;
}

std::optional<unsigned> InputMapper::add_gamepad(GamepadInstanceId instance) {
    if (const auto existing = find_pad(instance); existing.has_value())
        return pads_[*existing].port;
    for (std::size_t slot = 0; slot < pads_.size(); ++slot) {
        if (pads_[slot].present)
            continue;
        pads_[slot] = {};
        pads_[slot].present = true;
        pads_[slot].instance = instance;
        pads_[slot].state.instance = instance;
        return assign_first_free_port(slot);
    }
    return std::nullopt;
}

void InputMapper::remove_gamepad(GamepadInstanceId instance) {
    if (const auto slot = find_pad(instance); slot.has_value()) {
        pads_[*slot] = {};
        for (std::size_t candidate = 0; candidate < pads_.size(); ++candidate) {
            if (pads_[candidate].present && !pads_[candidate].port.has_value())
                static_cast<void>(assign_first_free_port(candidate));
        }
    }
}

bool InputMapper::update_gamepad(const GamepadState& state) {
    const auto slot = find_pad(state.instance);
    if (!slot.has_value())
        return false;
    if (focused_) {
        pads_[*slot].state = state;
        pads_[*slot].state.instance = pads_[*slot].instance;
    }
    return true;
}

void InputMapper::clear_live_inputs() {
    keys_.fill(false);
    for (auto& pad : pads_) {
        if (!pad.present)
            continue;
        const auto instance = pad.instance;
        pad.state = {};
        pad.state.instance = instance;
    }
}

void InputMapper::set_focused(bool focused) {
    if (focused_ == focused)
        return;
    focused_ = focused;
    if (!focused_)
        clear_live_inputs();
}

std::optional<GamepadInstanceId> InputMapper::assigned_gamepad(unsigned port) const {
    if (port >= bindings_.ports.size())
        return std::nullopt;
    for (const auto& pad : pads_) {
        if (pad.present && pad.port == port)
            return pad.instance;
    }
    return std::nullopt;
}

std::array<ControllerState, 4> InputMapper::controller_states() const {
    auto result = configured_;
    for (unsigned port_index = 0; port_index < result.size(); ++port_index) {
        auto& state = result[port_index];
        state.buttons = 0;
        state.stick_x = 0;
        state.stick_y = 0;
        if (!focused_)
            continue;

        const auto& port = bindings_.ports[port_index];
        const GamepadState* pad_state = nullptr;
        if (port.gamepad_enabled) {
            for (const auto& pad : pads_) {
                if (pad.present && pad.port == port_index) {
                    pad_state = &pad.state;
                    break;
                }
            }
        }

        for (unsigned button = 0; button < static_cast<unsigned>(N64Button::Count); ++button) {
            const auto target = static_cast<N64Button>(button);
            if (digital_active(port.buttons[button], port.keyboard_enabled, keys_, port.gamepad_enabled,
                               pad_state))
                state.buttons = static_cast<u16>(state.buttons | n64_button_mask(target));
        }

        s32 raw_x = 0;
        s32 raw_y = 0;
        if (pad_state != nullptr) {
            raw_x = normalized_axis(pad_state->axes[gamepad_axis_index(port.stick.x_axis)]);
            raw_y = normalized_axis(pad_state->axes[gamepad_axis_index(port.stick.y_axis)]);
            if (port.stick.invert_x)
                raw_x = -raw_x;
            if (port.stick.invert_y)
                raw_y = -raw_y;
        }
        if (port.keyboard_enabled) {
            const bool left = valid_keyboard_scancode(port.stick.left_key) &&
                              keys_[static_cast<std::size_t>(port.stick.left_key)];
            const bool right = valid_keyboard_scancode(port.stick.right_key) &&
                               keys_[static_cast<std::size_t>(port.stick.right_key)];
            const bool up = valid_keyboard_scancode(port.stick.up_key) &&
                            keys_[static_cast<std::size_t>(port.stick.up_key)];
            const bool down = valid_keyboard_scancode(port.stick.down_key) &&
                              keys_[static_cast<std::size_t>(port.stick.down_key)];
            const s32 key_x = right == left ? 0 : right ? AxisExtent : -AxisExtent;
            const s32 key_y = up == down ? 0 : up ? AxisExtent : -AxisExtent;
            raw_x = choose_stronger(raw_x, key_x);
            raw_y = choose_stronger(raw_y, key_y);
        }
        const auto [stick_x, stick_y] = map_stick(raw_x, raw_y, port.stick);
        state.stick_x = stick_x;
        state.stick_y = stick_y;
    }
    return result;
}

} // namespace cupid::host
