#include "desktop/linux/controls/input.hpp"
#include "../../support/test.hpp"
#include "host.hpp"
#include <gdk/gdk.h>

void linux_input_tests() {
  using namespace cupid::desktop;
  using namespace cupid::desktop::linux_host;
  Input input;
  input.key(GDK_KEY_j, true);
  input.key(GDK_KEY_space, true);
  input.key(GDK_KEY_w, true);
  auto keyboard = map_input(default_mapping(HostSource::Keyboard), input.poll());
  test::equal(keyboard.buttons, 0xa000);
  test::equal(static_cast<std::uint16_t>(keyboard.y), static_cast<std::uint16_t>(-32767));
  input.key(GDK_KEY_j, false);
  input.key(GDK_KEY_space, false);
  input.key(GDK_KEY_w, false);
  keyboard = map_input(default_mapping(HostSource::Keyboard), input.poll());
  test::equal(keyboard.buttons == 0 && keyboard.x == 0 && keyboard.y == 0, true);
  input.key(GDK_KEY_Left, true);
  input.key(GDK_KEY_Return, true);
  test::equal(map_input(default_mapping(HostSource::Keyboard), input.poll()).buttons, 0x1002);
  input.clear();
  test::equal(map_input(default_mapping(HostSource::Keyboard), input.poll()).buttons, 0);
  input.key(GDK_KEY_percent, true);
  input.key(GDK_KEY_apostrophe, true);
  test::equal(map_input(default_mapping(HostSource::Keyboard), input.poll()).buttons, 0);
  input.clear();
  input.key(GDK_KEY_2, true);
  input.key(GDK_KEY_at, false);
  auto custom = default_mapping(HostSource::Keyboard);
  custom.buttons[0] = {BindingKind::Key, '2'};
  test::equal(map_input(custom, input.poll()).buttons, 0);
  input.clear();
  const int device = SDL_JoystickAttachVirtual(
      SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
  if (device < 0)
    throw std::runtime_error(SDL_GetError());
  const auto joystick = SDL_JoystickOpen(device);
  if (!joystick)
    throw std::runtime_error(SDL_GetError());
  SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
  SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
  input.poll();
  SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 20000);
  SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTY, -12345);
  SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 1);
  auto host = input.poll();
  test::equal(host.gamepads[0].connected, true);
  const auto mapped = map_input(default_mapping(HostSource::Gamepad1), host);
  test::equal(mapped.buttons, 0x8000);
  test::equal(mapped.x, 20000);
  test::equal(static_cast<std::uint16_t>(mapped.y), static_cast<std::uint16_t>(-12345));
  SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
  SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_START, 1);
  test::equal(map_input(default_mapping(HostSource::Gamepad1), input.poll()).buttons, 0xb000);
  SDL_JoystickClose(joystick);
  test::equal(SDL_JoystickDetachVirtual(device), 0);
  host = input.poll();
  test::equal(host.gamepads[0].connected, false);
  test::equal(map_input(default_mapping(HostSource::Gamepad1), host).buttons, 0);
}
