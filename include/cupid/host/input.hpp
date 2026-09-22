#pragma once

#include "cupid/rcp/controller.hpp"
#include "cupid/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace cupid::host {

inline constexpr int KeyboardScancodeMin = 0;
inline constexpr int KeyboardScancodeMax = 511;
inline constexpr int KeyboardScancodeUnbound = -1;
inline constexpr std::size_t MaxInputSettingsTextBytes = 16U * 1024U;

using GamepadInstanceId = u64;

enum class GamepadButton : u8 {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count,
};

enum class GamepadAxis : u8 {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

enum class AxisDirection : s8 {
    Negative = -1,
    Positive = 1,
};

enum class N64Button : u8 {
    A,
    B,
    Start,
    Z,
    L,
    R,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    CUp,
    CDown,
    CLeft,
    CRight,
    Count,
};

[[nodiscard]] constexpr u16 n64_button_mask(N64Button button) {
    switch (button) {
    case N64Button::A:
        return 0x8000U;
    case N64Button::B:
        return 0x4000U;
    case N64Button::Z:
        return 0x2000U;
    case N64Button::Start:
        return 0x1000U;
    case N64Button::DpadUp:
        return 0x0800U;
    case N64Button::DpadDown:
        return 0x0400U;
    case N64Button::DpadLeft:
        return 0x0200U;
    case N64Button::DpadRight:
        return 0x0100U;
    case N64Button::L:
        return 0x0020U;
    case N64Button::R:
        return 0x0010U;
    case N64Button::CUp:
        return 0x0008U;
    case N64Button::CDown:
        return 0x0004U;
    case N64Button::CLeft:
        return 0x0002U;
    case N64Button::CRight:
        return 0x0001U;
    case N64Button::Count:
        return 0;
    }
    return 0;
}

struct GamepadState {
    GamepadInstanceId instance{};
    std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> buttons{};
    std::array<s16, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
};

struct DigitalBinding {
    int keyboard_scancode{KeyboardScancodeUnbound};
    GamepadButton gamepad_button{GamepadButton::Count};
    GamepadAxis gamepad_axis{GamepadAxis::Count};
    AxisDirection axis_direction{AxisDirection::Positive};
    u16 axis_threshold{16000};
};

struct StickBinding {
    int left_key{KeyboardScancodeUnbound};
    int right_key{KeyboardScancodeUnbound};
    int up_key{KeyboardScancodeUnbound};
    int down_key{KeyboardScancodeUnbound};
    GamepadAxis x_axis{GamepadAxis::LeftX};
    GamepadAxis y_axis{GamepadAxis::LeftY};
    bool invert_x{};
    bool invert_y{true};
    u16 deadzone{4096};
    u16 input_range{32767};
    u8 output_range{80};
};

struct PortInputBindings {
    bool keyboard_enabled{};
    bool gamepad_enabled{true};
    std::array<DigitalBinding, static_cast<std::size_t>(N64Button::Count)> buttons{};
    StickBinding stick{};
};

struct InputBindings {
    std::array<PortInputBindings, 4> ports{};
};

[[nodiscard]] bool valid_keyboard_scancode(int scancode);
[[nodiscard]] bool validate_input_bindings(const InputBindings& bindings, std::string& error);
[[nodiscard]] InputBindings default_input_bindings();
[[nodiscard]] std::string encode_input_bindings(const InputBindings& bindings);
[[nodiscard]] bool decode_input_bindings(std::string_view text, InputBindings& bindings, std::string& error);

class InputMapper {
  public:
    explicit InputMapper(std::array<ControllerState, 4> configured,
                         InputBindings bindings = default_input_bindings());

    [[nodiscard]] const InputBindings& bindings() const {
        return bindings_;
    }
    [[nodiscard]] bool set_bindings(const InputBindings& bindings, std::string& error);
    void set_configured_state(unsigned port, ControllerState state);

    [[nodiscard]] bool set_keyboard_scancode(int scancode, bool pressed);
    [[nodiscard]] std::optional<unsigned> add_gamepad(GamepadInstanceId instance);
    void remove_gamepad(GamepadInstanceId instance);
    [[nodiscard]] bool update_gamepad(const GamepadState& state);

    void set_focused(bool focused);
    [[nodiscard]] bool focused() const {
        return focused_;
    }
    [[nodiscard]] std::optional<GamepadInstanceId> assigned_gamepad(unsigned port) const;
    [[nodiscard]] std::array<ControllerState, 4> controller_states() const;

  private:
    struct PadSlot {
        bool present{};
        GamepadInstanceId instance{};
        GamepadState state{};
        std::optional<unsigned> port;
    };

    [[nodiscard]] std::optional<std::size_t> find_pad(GamepadInstanceId instance) const;
    [[nodiscard]] std::optional<unsigned> assign_first_free_port(std::size_t slot);
    void clear_live_inputs();

    std::array<ControllerState, 4> configured_{};
    InputBindings bindings_{};
    std::array<bool, static_cast<std::size_t>(KeyboardScancodeMax + 1)> keys_{};
    std::array<PadSlot, 4> pads_{};
    bool focused_{true};
};

} // namespace cupid::host
