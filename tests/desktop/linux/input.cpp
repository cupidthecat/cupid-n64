#include "desktop/linux/controls/input.hpp"
#include "../../support/test.hpp"
#include "host.hpp"
#include <gdk/gdk.h>

namespace {
struct Rumble {
  unsigned calls = 0;
  Uint16 low = 0, high = 0;
};

bool SDLCALL rumble(void *userdata, Uint16 low, Uint16 high) {
  auto &state = *static_cast<Rumble *>(userdata);
  ++state.calls;
  state.low = low;
  state.high = high;
  return true;
}
} // namespace

void linux_input_tests() {
  using namespace cupid::desktop;
  using namespace cupid::desktop::linux_host;
  Input input;
  const auto key = [&input](unsigned keyval, bool pressed) {
    input.key(keyval, pressed, key_code(keyval));
  };
  key(GDK_KEY_j, true);
  key(GDK_KEY_space, true);
  key(GDK_KEY_w, true);
  auto keyboard = map_input(default_mapping(HostSource::Keyboard), input.poll());
  test::equal(keyboard.buttons, 0xa000);
  test::equal(static_cast<std::uint16_t>(keyboard.y), static_cast<std::uint16_t>(-32767));
  key(GDK_KEY_j, false);
  key(GDK_KEY_space, false);
  key(GDK_KEY_w, false);
  keyboard = map_input(default_mapping(HostSource::Keyboard), input.poll());
  test::equal(keyboard.buttons == 0 && keyboard.x == 0 && keyboard.y == 0, true);
  key(GDK_KEY_Left, true);
  key(GDK_KEY_Return, true);
  test::equal(map_input(default_mapping(HostSource::Keyboard), input.poll()).buttons, 0x1002);
  input.clear();
  test::equal(map_input(default_mapping(HostSource::Keyboard), input.poll()).buttons, 0);
  key(GDK_KEY_percent, true);
  key(GDK_KEY_apostrophe, true);
  test::equal(map_input(default_mapping(HostSource::Keyboard), input.poll()).buttons, 0);
  input.clear();
  key(GDK_KEY_2, true);
  key(GDK_KEY_at, false);
  auto custom = default_mapping(HostSource::Keyboard);
  custom.buttons[0] = {BindingKind::Key, '2'};
  test::equal(map_input(custom, input.poll()).buttons, 0);
  input.clear();
  key(GDK_KEY_2, true);
  input.key(GDK_KEY_quotedbl, false, key_code(GDK_KEY_2));
  test::equal(map_input(custom, input.poll()).buttons, 0);
  input.clear();
  input.key(GDK_KEY_Shift_L, true, 40);
  input.key(GDK_KEY_Shift_R, true, 41);
  input.key(GDK_KEY_Shift_L, false, 40);
  test::equal(input.poll().keys[0x10], true);
  input.key(GDK_KEY_Shift_R, false, 41);
  test::equal(input.poll().keys[0x10], false);
  SDL_VirtualJoystickDesc descriptor{};
  SDL_INIT_INTERFACE(&descriptor);
  descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
  descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
  descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  Rumble feedback;
  descriptor.userdata = &feedback;
  descriptor.Rumble = rumble;
  const auto device = SDL_AttachVirtualJoystick(&descriptor);
  if (!device)
    throw std::runtime_error(SDL_GetError());
  const auto joystick = SDL_OpenJoystick(device);
  if (!joystick)
    throw std::runtime_error(SDL_GetError());
  test::equal(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768), true);
  test::equal(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768), true);
  input.poll();
  SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 20000);
  SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -12345);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, 1);
  auto host = input.poll();
  test::equal(host.gamepads[0].connected, true);
  test::equal(host.gamepads[0].feedback, true);
  input.rumble({true, false, false, false});
  test::equal(feedback.calls > 0 && feedback.low == 0xffff && feedback.high == 0xffff, true);
  input.stop();
  test::equal(feedback.low == 0 && feedback.high == 0, true);
  const auto mapped = map_input(default_mapping(HostSource::Gamepad1), host);
  test::equal(mapped.buttons, 0x8000);
  test::equal(mapped.x, 20000);
  test::equal(static_cast<std::uint16_t>(mapped.y), static_cast<std::uint16_t>(-12345));
  SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, 1);
  test::equal(map_input(default_mapping(HostSource::Gamepad1), input.poll()).buttons, 0xb000);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_BACK, 1);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_WEST, 1);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_NORTH, 1);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 1);
  SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 1);
  host = input.poll();
  test::equal((host.gamepads[0].buttons & 0xc320) == 0xc320, true);
  SDL_CloseJoystick(joystick);
  test::equal(SDL_DetachVirtualJoystick(device), true);
  host = input.poll();
  test::equal(host.gamepads[0].connected, false);
  test::equal(map_input(default_mapping(HostSource::Gamepad1), host).buttons, 0);
}
