#pragma once

#include "desktop/controls/input.hpp"
#include <string_view>

namespace cupid::desktop {

enum class HostSource { None, Keyboard, Gamepad1, Gamepad2, Gamepad3, Gamepad4 };
enum class BindingKind { None, Key, GamepadButton, AxisNegative, AxisPositive };
enum class HostAxis { None, LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };

struct InputBinding {
  BindingKind kind = BindingKind::None;
  unsigned code = 0;
  bool operator==(const InputBinding &) const = default;
};

struct AxisMapping {
  HostAxis source = HostAxis::None;
  bool inverted = false;
  InputBinding negative, positive;
  bool operator==(const AxisMapping &) const = default;
};

struct ControllerMapping {
  HostSource source = HostSource::None;
  std::array<InputBinding, 16> buttons{};
  std::array<AxisMapping, 6> axes{};
  InputBinding slow;
  bool feedback = true;
  bool operator==(const ControllerMapping &) const = default;
};

struct HostGamepad {
  bool connected = false;
  bool feedback = false;
  std::uint16_t buttons = 0;
  std::array<std::int16_t, 6> axes{};
};

struct HostInputs {
  std::array<bool, 256> keys{};
  std::array<HostGamepad, 4> gamepads{};
};

inline constexpr std::array<std::wstring_view, 16> button_names{
    L"A", L"B", L"Z",    L"Start",  L"D-pad Up", L"D-pad Down", L"D-pad Left", L"D-pad Right",
    L"L", L"R", L"C Up", L"C Down", L"C Left",   L"C Right",    L"GameCube X", L"GameCube Y"};
inline constexpr std::array<std::wstring_view, 6> axis_names{
    L"Main stick X",       L"Main stick Y",          L"GameCube C stick X",
    L"GameCube C stick Y", L"GameCube left trigger", L"GameCube right trigger"};

ControllerMapping default_mapping(HostSource source);
bool valid_binding(InputBinding binding);
void validate_mapping(const ControllerMapping &mapping);
PortInput map_input(const ControllerMapping &mapping, const HostInputs &host);
std::array<bool, 4> feedback_targets(const std::array<ControllerMapping, 4> &mappings,
                                     const std::array<bool, 4> &motors, const HostInputs &host,
                                     bool enabled);

} // namespace cupid::desktop
