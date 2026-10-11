#include "desktop/controls/mapping.hpp"
#include "core/controller/gamecube/gamecube.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace cupid::desktop {
namespace {

const HostGamepad *gamepad(const ControllerMapping &mapping, const HostInputs &host) {
  if (mapping.source < HostSource::Gamepad1 || mapping.source > HostSource::Gamepad4)
    return nullptr;
  const auto &pad = host.gamepads[static_cast<unsigned>(mapping.source) -
                                  static_cast<unsigned>(HostSource::Gamepad1)];
  return pad.connected ? &pad : nullptr;
}

bool pressed(InputBinding binding, const ControllerMapping &mapping, const HostInputs &host) {
  if (binding.kind == BindingKind::Key)
    return mapping.source == HostSource::Keyboard && binding.code < host.keys.size() &&
           host.keys[binding.code];
  const auto *pad = gamepad(mapping, host);
  if (!pad)
    return false;
  if (binding.kind == BindingKind::GamepadButton)
    return (pad->buttons & binding.code) != 0;
  if (binding.code >= pad->axes.size())
    return false;
  if (binding.kind == BindingKind::AxisNegative)
    return pad->axes[binding.code] < -16384;
  if (binding.kind == BindingKind::AxisPositive)
    return pad->axes[binding.code] > 16384;
  return false;
}

std::int16_t axis(const AxisMapping &mapping, const ControllerMapping &controller,
                  const HostInputs &host) {
  int value = 0;
  if (const auto *pad = gamepad(controller, host);
      pad && mapping.source >= HostAxis::LeftX && mapping.source <= HostAxis::RightTrigger)
    value = pad->axes[static_cast<unsigned>(mapping.source) - 1];
  value += (int(pressed(mapping.positive, controller, host)) -
            int(pressed(mapping.negative, controller, host))) *
           32767;
  if (mapping.inverted)
    value = -value;
  return static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
}

} // namespace

ControllerMapping default_mapping(HostSource source) {
  ControllerMapping mapping;
  mapping.source = source;
  if (source < HostSource::Gamepad1) {
    constexpr std::array<unsigned, 16> keys{'J', 'K', 0x20, 0x0d, 'I',  'O',  'U', 'P',
                                            'Q', 'E', 0x26, 0x28, 0x25, 0x27, 'H', 'L'};
    for (unsigned n = 0; n < keys.size(); ++n)
      mapping.buttons[n] = {BindingKind::Key, keys[n]};
    constexpr std::array<std::array<unsigned, 2>, 6> axes{
        {{'A', 'D'}, {'W', 'S'}, {0x25, 0x27}, {0x26, 0x28}, {0, 'Q'}, {0, 'E'}}};
    for (unsigned n = 0; n < axes.size(); ++n) {
      mapping.axes[n].negative =
          axes[n][0] ? InputBinding{BindingKind::Key, axes[n][0]} : InputBinding{};
      mapping.axes[n].positive = {BindingKind::Key, axes[n][1]};
    }
    mapping.slow = {BindingKind::Key, 0x10};
  } else {
    constexpr std::array<unsigned, 16> buttons{0x1000, 0x2000, 0, 0x10, 1, 2, 4,      8,
                                               0x100,  0x200,  0, 0,    0, 0, 0x4000, 0x8000};
    for (unsigned n = 0; n < buttons.size(); ++n)
      if (buttons[n])
        mapping.buttons[n] = {BindingKind::GamepadButton, buttons[n]};
    mapping.buttons[2] = {BindingKind::AxisPositive, 4};
    mapping.buttons[10] = {BindingKind::AxisNegative, 3};
    mapping.buttons[11] = {BindingKind::AxisPositive, 3};
    mapping.buttons[12] = {BindingKind::AxisNegative, 2};
    mapping.buttons[13] = {BindingKind::AxisPositive, 2};
    for (unsigned n = 0; n < mapping.axes.size(); ++n)
      mapping.axes[n].source = static_cast<HostAxis>(n + 1);
  }
  return mapping;
}

bool valid_binding(InputBinding binding) {
  switch (binding.kind) {
  case BindingKind::None:
    return binding.code == 0;
  case BindingKind::Key:
    return binding.code > 0 && binding.code < 256;
  case BindingKind::GamepadButton:
    return binding.code <= 0xffff && std::has_single_bit(binding.code) &&
           (binding.code & 0xf3ff) != 0;
  case BindingKind::AxisNegative:
  case BindingKind::AxisPositive:
    return binding.code < 6;
  }
  return false;
}

void validate_mapping(const ControllerMapping &mapping) {
  if (mapping.source < HostSource::None || mapping.source > HostSource::Gamepad4 ||
      !valid_binding(mapping.slow))
    throw std::runtime_error("Select a supported host input device and binding.");
  for (const auto button : mapping.buttons)
    if (!valid_binding(button))
      throw std::runtime_error("Select a supported button binding.");
  for (const auto &axis : mapping.axes)
    if (axis.source < HostAxis::None || axis.source > HostAxis::RightTrigger ||
        !valid_binding(axis.negative) || !valid_binding(axis.positive))
      throw std::runtime_error("Select a supported analog axis and direction binding.");
}

PortInput map_input(const ControllerMapping &mapping, const HostInputs &host) {
  PortInput input;
  if (mapping.source == HostSource::None ||
      (mapping.source != HostSource::Keyboard && !gamepad(mapping, host)))
    return input;
  constexpr std::array<std::uint16_t, 16> masks{0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400,
                                                0x0200, 0x0100, 0x0020, 0x0010, 0x0008, 0x0004,
                                                0x0002, 0x0001, 0,      0};
  using Pad = n64::GameCubePad;
  constexpr std::array<std::uint16_t, 16> gamecube{
      Pad::A, Pad::B, Pad::Z, Pad::Start, Pad::Up, Pad::Down, Pad::Left, Pad::Right,
      Pad::L, Pad::R, 0,      0,          0,       0,         Pad::X,    Pad::Y};
  for (unsigned n = 0; n < mapping.buttons.size(); ++n) {
    if (!pressed(mapping.buttons[n], mapping, host))
      continue;
    input.buttons |= masks[n];
    input.gamecube_buttons |= gamecube[n];
  }
  std::array<std::int16_t, 6> axes{};
  for (unsigned n = 0; n < axes.size(); ++n)
    axes[n] = axis(mapping.axes[n], mapping, host);
  input.x = axes[0];
  input.y = axes[1];
  if (pressed(mapping.slow, mapping, host)) {
    constexpr int walking_magnitude = 12900;
    input.x = static_cast<std::int16_t>(int(input.x) * walking_magnitude / 32767);
    input.y = static_cast<std::int16_t>(int(input.y) * walking_magnitude / 32767);
  }
  input.cx = axes[2];
  input.cy = axes[3];
  input.l = axes[4];
  input.r = axes[5];
  return input;
}

std::array<bool, 4> feedback_targets(const std::array<ControllerMapping, 4> &mappings,
                                     const std::array<bool, 4> &motors, const HostInputs &host,
                                     bool enabled) {
  std::array<bool, 4> targets{};
  if (!enabled)
    return targets;
  for (unsigned port = 0; port < mappings.size(); ++port) {
    const auto &mapping = mappings[port];
    const auto *pad = gamepad(mapping, host);
    if (!motors[port] || !mapping.feedback || !pad || !pad->feedback)
      continue;
    const auto device =
        static_cast<unsigned>(mapping.source) - static_cast<unsigned>(HostSource::Gamepad1);
    targets[device] = true;
  }
  return targets;
}

} // namespace cupid::desktop
