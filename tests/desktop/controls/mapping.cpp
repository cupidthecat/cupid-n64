#include "desktop/controls/mapping.hpp"
#include "../../support/test.hpp"
#include "core/controller/accessories/cartridge/handheld.hpp"
#include "core/controller/gamecube/gamecube.hpp"
#include "core/controller/gamepad.hpp"
#include "core/controller/stick/response.hpp"
#include "desktop/controls/feedback.hpp"
#include "desktop/controls/ports.hpp"

namespace {

using namespace cupid::desktop;

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

std::uint32_t report(cupid::n64::Gamepad &pad) {
  const std::array<std::uint8_t, 1> command{1};
  std::array<std::uint8_t, 4> bytes{};
  test::equal(pad.communicate(command, bytes).valid, true);
  return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
         (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

void keyboard() {
  auto mapping = default_mapping(HostSource::Keyboard);
  validate_mapping(mapping);
  HostInputs host;
  host.keys['J'] = host.keys['D'] = true;
  auto input = map_input(mapping, host);
  cupid::n64::RandomGenerator random;
  cupid::n64::Gamepad pad(random);
  pad.input_host(input.buttons, input.x, input.y);
  test::equal(report(pad), 0x80005500);
  host.keys[0x10] = true;
  input = map_input(mapping, host);
  pad.input_host(input.buttons, input.x, input.y);
  test::equal(report(pad), 0x80002300);
  host.keys['A'] = true;
  test::equal(map_input(mapping, host).x, 0);
  host.keys['J'] = false;
  test::equal(map_input(mapping, host).buttons, 0);
  host = {};
  host.keys['H'] = true;
  host.keys[0x27] = true;
  input = map_input(mapping, host);
  test::equal(input.gamecube_buttons, cupid::n64::GameCubePad::X);
  test::equal(input.buttons, 1);
  test::equal(input.cx, 32767);
  mapping.buttons[0] = {BindingKind::Key, 'B'};
  host = {};
  host.keys['B'] = true;
  test::equal(map_input(mapping, host).buttons, 0x8000);
  mapping.buttons[0] = {};
  test::equal(map_input(mapping, host).buttons, 0);
  mapping.source = HostSource::None;
  test::equal(map_input(mapping, host).gamecube_buttons, 0);
}

void gamepads() {
  HostInputs host;
  constexpr std::array<std::uint16_t, 4> buttons{0x1000, 0x2000, 0x10, 0x100};
  constexpr std::array<std::uint16_t, 4> expected{0x8000, 0x4000, 0x1000, 0x20};
  for (unsigned port = 0; port < 4; ++port) {
    auto &pad = host.gamepads[port];
    pad.connected = true;
    pad.buttons = buttons[port];
    pad.axes[0] = 32767;
    pad.axes[1] = port == 1 ? -32767 : 0;
  }
  cupid::n64::RandomGenerator random;
  for (unsigned port = 0; port < 4; ++port) {
    auto mapping = default_mapping(
        static_cast<HostSource>(static_cast<unsigned>(HostSource::Gamepad1) + port));
    validate_mapping(mapping);
    auto input = map_input(mapping, host);
    test::equal(input.buttons, expected[port]);
    cupid::n64::Gamepad pad(random);
    pad.input_host(input.buttons, input.x, input.y);
    test::equal(report(pad), (std::uint32_t(expected[port]) << 16) | (port == 1 ? 0x4545 : 0x5500));
    host.gamepads[port].connected = false;
    input = map_input(mapping, host);
    test::equal(input.buttons, 0);
    test::equal(input.x, 0);
    test::equal(input.y, 0);
    test::equal(input.l, 0);
    host.gamepads[port].connected = true;
    mapping.axes[0].source = HostAxis::RightX;
    mapping.axes[0].inverted = true;
    host.gamepads[port].axes[2] = -32768;
    test::equal(map_input(mapping, host).x, 32767);
  }
  auto mapping = default_mapping(HostSource::Gamepad1);
  host.gamepads[0] = {true, true, 0, {2000, -2000, 32767, -32767, 32767, 20000}};
  const auto input = map_input(mapping, host);
  test::equal(input.buttons, 0x2009);
  test::equal(input.l, 32767);
  test::equal(input.r, 20000);
  const auto stick = cupid::n64::stick_response(input.x, input.y);
  test::equal(stick.x, 0);
  test::equal(stick.y, 0);
  test::equal(map_input(mapping, HostInputs{}).buttons, 0);
}

void validation() {
  auto mapping = default_mapping(HostSource::Keyboard);
  mapping.buttons[0] = {BindingKind::Key, 256};
  test::equal(rejects([&] { validate_mapping(mapping); }), true);
  mapping.buttons[0] = {BindingKind::GamepadButton, 3};
  test::equal(rejects([&] { validate_mapping(mapping); }), true);
  mapping.buttons[0] = {BindingKind::AxisNegative, 6};
  test::equal(rejects([&] { validate_mapping(mapping); }), true);
  mapping = default_mapping(HostSource::Keyboard);
  mapping.axes[0].source = static_cast<HostAxis>(8);
  test::equal(rejects([&] { validate_mapping(mapping); }), true);
  ControllerPorts ports;
  validate_ports(ports);
  ports[3].heart_rate = 181;
  test::equal(rejects([&] { validate_ports(ports); }), true);
  ports[3].heart_rate = 30;
  ports[2].controller = static_cast<ControllerType>(9);
  test::equal(rejects([&] { validate_ports(ports); }), true);
}

void feedback() {
  std::array<ControllerMapping, 4> mappings{
      default_mapping(HostSource::Gamepad4), default_mapping(HostSource::Gamepad2),
      default_mapping(HostSource::Gamepad4), default_mapping(HostSource::Keyboard)};
  HostInputs host;
  host.gamepads[1].connected = host.gamepads[1].feedback = true;
  host.gamepads[3].connected = host.gamepads[3].feedback = true;
  const auto mask = [](const std::array<bool, 4> &motors) {
    unsigned value = 0;
    for (unsigned n = 0; n < motors.size(); ++n)
      value |= unsigned(motors[n]) << n;
    return value;
  };
  std::array<bool, 4> motors{true, true, false, true};
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 10);
  test::equal(mask(feedback_targets(mappings, motors, host, false)), 0);
  mappings[0].feedback = false;
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 2);
  motors[2] = true;
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 10);
  host.gamepads[3].connected = false;
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 2);
  host.gamepads[1].feedback = false;
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 0);
  mappings[1].source = HostSource::Gamepad1;
  host.gamepads[0].connected = host.gamepads[0].feedback = true;
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 1);
  motors.fill(false);
  test::equal(mask(feedback_targets(mappings, motors, host, true)), 0);
  FeedbackLatch latch;
  std::array<FeedbackState, 4> state{{{true, 7}, {true, 3}, {}, {}}};
  test::equal(mask(latch.update(state)), 3);
  latch.reset(state);
  test::equal(mask(latch.update(state)), 0);
  ++state[0].commands;
  test::equal(mask(latch.update(state)), 1);
  state[1].active = false;
  test::equal(mask(latch.update(state)), 1);
  state[1].active = true;
  test::equal(mask(latch.update(state)), 3);
  latch.reset(state);
  test::equal(mask(latch.update(state)), 0);
  latch.clear();
  test::equal(mask(latch.update(state)), 3);
}

void motor_commands() {
  cupid::n64::RandomGenerator random;
  cupid::n64::Gamepad pad(random);
  pad.rumble_pak();
  std::array<std::uint8_t, 3> identity{};
  pad.communicate(std::array<std::uint8_t, 1>{0}, identity);
  test::equal(pad.rumble_commands(), 0);
  std::array<std::uint8_t, 35> write{};
  write[0] = 3;
  write[1] = 0xc0;
  write[2] = static_cast<std::uint8_t>(cupid::n64::address_crc(0xc000));
  write[3] = 1;
  std::array<std::uint8_t, 1> response{};
  for (unsigned command = 1; command <= 2; ++command) {
    test::equal(pad.communicate(write, response).valid, true);
    test::equal(pad.rumbling(), true);
    test::equal(pad.rumble_commands(), command);
  }
  write[1] = 0x80;
  write[2] = static_cast<std::uint8_t>(cupid::n64::address_crc(0x8000));
  pad.communicate(write, response);
  test::equal(pad.rumble_commands(), 2);
  cupid::n64::GameCubePad gamecube;
  std::array<std::uint8_t, 8> report{};
  for (unsigned command = 1; command <= 2; ++command) {
    test::equal(gamecube.communicate(std::array<std::uint8_t, 3>{0x40, 3, 1}, report).valid, true);
    test::equal(gamecube.rumbling(), true);
    test::equal(gamecube.rumble_commands(), command);
  }
  cupid::n64::HandheldCartridge handheld;
  std::vector<std::uint8_t> rom(0x8000);
  rom[0x147] = 0x1c;
  test::equal(handheld.load(rom), true);
  handheld.write(0x2000, 1);
  test::equal(handheld.rumble_commands(), 0);
  for (unsigned command = 1; command <= 2; ++command) {
    handheld.write(0x4000, 8);
    test::equal(handheld.rumbling(), true);
    test::equal(handheld.rumble_commands(), command);
  }
}

} // namespace

int main() {
  keyboard();
  gamepads();
  feedback();
  motor_commands();
  validation();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures != 0;
}
