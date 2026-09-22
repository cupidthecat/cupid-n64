#include "application_internal.hpp"

#include <algorithm>
#include <array>

namespace cupid::desktop {
namespace {

constexpr std::array<std::string_view, 14> button_names{"A",    "B",       "Start",   "Z",       "L",
                                                        "R",    "D-pad U", "D-pad D", "D-pad L", "D-pad R",
                                                        "C-up", "C-down",  "C-left",  "C-right"};
constexpr std::array<std::string_view, 15> pad_names{"South/A", "East/B",  "West/X",  "North/Y", "Back",
                                                     "Guide",   "Start",   "L-stick", "R-stick", "LB",
                                                     "RB",      "D-pad U", "D-pad D", "D-pad L", "D-pad R"};
constexpr std::array<std::string_view, 6> axis_names{"LX", "LY", "RX", "RY", "LT", "RT"};

std::string key_name(int scancode) {
    if (scancode == host::KeyboardScancodeUnbound)
        return "Unset";
    const char* label = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
    return label && *label ? label : "Key " + std::to_string(scancode);
}

std::string pad_name(const host::DigitalBinding& binding) {
    if (binding.gamepad_button != host::GamepadButton::Count)
        return std::string(pad_names.at(static_cast<std::size_t>(binding.gamepad_button)));
    if (binding.gamepad_axis != host::GamepadAxis::Count)
        return std::string(axis_names.at(static_cast<std::size_t>(binding.gamepad_axis))) +
               (binding.axis_direction == host::AxisDirection::Negative ? " -" : " +");
    return "Unset";
}

} // namespace

void Application::draw_controls() {
    const auto& port = mapper.bindings().ports[input_port];
    button(12, 45, 86, "Port " + std::to_string(input_port + 1),
           [this] { input_port = (input_port + 1) % 4; });
    button(104, 45, 132, port.keyboard_enabled ? "Keyboard on" : "Keyboard off", [this] {
        auto bindings = mapper.bindings();
        bindings.ports[input_port].keyboard_enabled = !bindings.ports[input_port].keyboard_enabled;
        change_bindings(std::move(bindings));
    });
    button(242, 45, 132, port.gamepad_enabled ? "Gamepad on" : "Gamepad off", [this] {
        auto bindings = mapper.bindings();
        bindings.ports[input_port].gamepad_enabled = !bindings.ports[input_port].gamepad_enabled;
        change_bindings(std::move(bindings));
    });
    button(380, 45, 168, "Reset port bindings", [this] {
        auto bindings = mapper.bindings();
        bindings.ports[input_port] = host::default_input_bindings().ports[input_port];
        change_bindings(std::move(bindings));
    });
    text(12, 78, fit_text(input.name(input_port), 67), true);
    for (unsigned index = 0; index < button_names.size(); ++index) {
        const float x = index < 7 ? 12.0F : 286.0F;
        const float y = 97 + static_cast<float>(index % 7) * 26;
        text(x, y + 7, button_names[index]);
        button(x + 68, y, 74, key_name(port.buttons[index].keyboard_scancode),
               [this, index] { begin_capture(index, true); });
        button(
            x + 148, y, 114, pad_name(port.buttons[index]), [this, index] { begin_capture(index, false); },
            mapper.assigned_gamepad(input_port).has_value());
    }
    constexpr unsigned count = static_cast<unsigned>(host::N64Button::Count);
    const std::array<int, 4> keys{port.stick.left_key, port.stick.right_key, port.stick.up_key,
                                  port.stick.down_key};
    constexpr std::array<std::string_view, 4> directions{"Left", "Right", "Up", "Down"};
    text(12, 291, "Stick keys", true);
    for (unsigned index = 0; index < keys.size(); ++index)
        button(102 + static_cast<float>(index) * 112, 283, 106,
               std::string(directions[index]) + ' ' + key_name(keys[index]),
               [this, index] { begin_capture(count + index, true); });
    const bool gamepad = mapper.assigned_gamepad(input_port).has_value();
    button(
        12, 311, 78, "X " + std::string(axis_names.at(static_cast<std::size_t>(port.stick.x_axis))),
        [this] { begin_capture(count + 4, false); }, gamepad);
    button(
        96, 311, 78, "Y " + std::string(axis_names.at(static_cast<std::size_t>(port.stick.y_axis))),
        [this] { begin_capture(count + 5, false); }, gamepad);
    button(180, 311, 104, port.stick.invert_x ? "Invert X on" : "Invert X off", [this] {
        auto bindings = mapper.bindings();
        bindings.ports[input_port].stick.invert_x = !bindings.ports[input_port].stick.invert_x;
        change_bindings(std::move(bindings));
    });
    button(290, 311, 104, port.stick.invert_y ? "Invert Y on" : "Invert Y off", [this] {
        auto bindings = mapper.bindings();
        bindings.ports[input_port].stick.invert_y = !bindings.ports[input_port].stick.invert_y;
        change_bindings(std::move(bindings));
    });
    button(400, 311, 148, "Deadzone " + std::to_string(port.stick.deadzone), [this] {
        auto bindings = mapper.bindings();
        auto& stick = bindings.ports[input_port].stick;
        const unsigned next = stick.deadzone + 1024U;
        stick.deadzone = static_cast<u16>(next >= stick.input_range || next > 12288 ? 0 : next);
        change_bindings(std::move(bindings));
    });
}

} // namespace cupid::desktop
