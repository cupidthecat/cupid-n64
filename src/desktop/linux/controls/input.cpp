#include "desktop/linux/controls/input.hpp"
#include <algorithm>
#include <gdk/gdk.h>

namespace cupid::desktop::linux_host {

unsigned key_code(unsigned keyval) {
  if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F24)
    return 0x70 + keyval - GDK_KEY_F1;
  switch (keyval) {
  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    return 0x0d;
  case GDK_KEY_Shift_L:
  case GDK_KEY_Shift_R:
    return 0x10;
  case GDK_KEY_Control_L:
  case GDK_KEY_Control_R:
    return 0x11;
  case GDK_KEY_Alt_L:
  case GDK_KEY_Alt_R:
    return 0x12;
  case GDK_KEY_space:
    return 0x20;
  case GDK_KEY_Left:
    return 0x25;
  case GDK_KEY_Up:
    return 0x26;
  case GDK_KEY_Right:
    return 0x27;
  case GDK_KEY_Down:
    return 0x28;
  case GDK_KEY_percent:
    return '5';
  case GDK_KEY_exclam:
    return '1';
  case GDK_KEY_at:
    return '2';
  case GDK_KEY_numbersign:
    return '3';
  case GDK_KEY_dollar:
    return '4';
  case GDK_KEY_asciicircum:
    return '6';
  case GDK_KEY_ampersand:
    return '7';
  case GDK_KEY_asterisk:
    return '8';
  case GDK_KEY_parenleft:
    return '9';
  case GDK_KEY_parenright:
    return '0';
  case GDK_KEY_apostrophe:
  case GDK_KEY_quotedbl:
    return 0xde;
  case GDK_KEY_semicolon:
  case GDK_KEY_colon:
    return 0xba;
  case GDK_KEY_equal:
  case GDK_KEY_plus:
    return 0xbb;
  case GDK_KEY_comma:
  case GDK_KEY_less:
    return 0xbc;
  case GDK_KEY_minus:
  case GDK_KEY_underscore:
    return 0xbd;
  case GDK_KEY_period:
  case GDK_KEY_greater:
    return 0xbe;
  case GDK_KEY_slash:
  case GDK_KEY_question:
    return 0xbf;
  case GDK_KEY_grave:
  case GDK_KEY_asciitilde:
    return 0xc0;
  case GDK_KEY_bracketleft:
  case GDK_KEY_braceleft:
    return 0xdb;
  case GDK_KEY_backslash:
  case GDK_KEY_bar:
    return 0xdc;
  case GDK_KEY_bracketright:
  case GDK_KEY_braceright:
    return 0xdd;
  default:
    const auto character = gdk_keyval_to_unicode(gdk_keyval_to_upper(keyval));
    return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')
               ? character
               : 0;
  }
}

Input::Input() {
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  initialized_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}

Input::~Input() {
  stop();
  for (auto controller : controllers_)
    if (controller)
      SDL_CloseGamepad(controller);
  if (initialized_)
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void Input::key(unsigned keyval, bool pressed, unsigned hardware_code) {
  if (pressed) {
    const auto entry = pressed_keys_.try_emplace(hardware_code, key_code(keyval)).first;
    if (const auto code = entry->second)
      host_.keys[code] = true;
  } else if (const auto entry = pressed_keys_.find(hardware_code); entry != pressed_keys_.end()) {
    const auto code = entry->second;
    pressed_keys_.erase(entry);
    if (code)
      host_.keys[code] = std::any_of(pressed_keys_.begin(), pressed_keys_.end(),
                                     [code](const auto &key) { return key.second == code; });
  }
}

void Input::clear() {
  host_.keys.fill(false);
  pressed_keys_.clear();
  stop();
}

void Input::enumerate() {
  for (unsigned n = 0; n < controllers_.size(); ++n)
    if (controllers_[n] && !SDL_GamepadConnected(controllers_[n])) {
      SDL_CloseGamepad(controllers_[n]);
      controllers_[n] = nullptr;
      motors_[n] = false;
    }
  int count = 0;
  const auto devices = SDL_GetGamepads(&count);
  for (int device = 0; device < count; ++device) {
    const auto id = devices[device];
    const bool opened = std::any_of(controllers_.begin(), controllers_.end(), [&](auto controller) {
      return controller && SDL_GetGamepadID(controller) == id;
    });
    if (opened)
      continue;
    const auto slot = std::find(controllers_.begin(), controllers_.end(), nullptr);
    if (slot == controllers_.end())
      break;
    *slot = SDL_OpenGamepad(id);
  }
  SDL_free(devices);
}

HostInputs Input::poll() {
  if (!initialized_)
    return host_;
  SDL_UpdateGamepads();
  enumerate();
  constexpr std::array<SDL_GamepadButton, 12> buttons{
      SDL_GAMEPAD_BUTTON_DPAD_UP,       SDL_GAMEPAD_BUTTON_DPAD_DOWN,
      SDL_GAMEPAD_BUTTON_DPAD_LEFT,     SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
      SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_BACK,
      SDL_GAMEPAD_BUTTON_LEFT_STICK,    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
      SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
      SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST};
  constexpr std::array<unsigned, 12> masks{1,    2,    4,     8,     0x10,   0x20,
                                           0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000};
  constexpr std::array<SDL_GamepadAxis, 6> axes{
      SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
      SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
  for (unsigned n = 0; n < controllers_.size(); ++n) {
    auto &pad = host_.gamepads[n];
    pad = {};
    if (!controllers_[n])
      continue;
    pad.connected = true;
    pad.feedback = SDL_GetBooleanProperty(SDL_GetGamepadProperties(controllers_[n]),
                                          SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    for (unsigned b = 0; b < buttons.size(); ++b)
      if (SDL_GetGamepadButton(controllers_[n], buttons[b]))
        pad.buttons |= masks[b];
    if (SDL_GetGamepadButton(controllers_[n], SDL_GAMEPAD_BUTTON_WEST))
      pad.buttons |= 0x4000;
    if (SDL_GetGamepadButton(controllers_[n], SDL_GAMEPAD_BUTTON_NORTH))
      pad.buttons |= 0x8000;
    for (unsigned a = 0; a < axes.size(); ++a)
      pad.axes[a] = SDL_GetGamepadAxis(controllers_[n], axes[a]);
  }
  return host_;
}

void Input::rumble(const std::array<bool, 4> &motors) {
  const auto now = std::chrono::steady_clock::now();
  const bool renew = now - renewed_ >= std::chrono::milliseconds(500);
  for (unsigned n = 0; n < motors.size(); ++n) {
    const bool active = motors[n] && controllers_[n] && host_.gamepads[n].feedback;
    if (active == motors_[n] && (!active || !renew))
      continue;
    if (controllers_[n])
      SDL_RumbleGamepad(controllers_[n], active ? 0xffff : 0, active ? 0xffff : 0, 1000);
    motors_[n] = active;
  }
  if (renew)
    renewed_ = now;
}

void Input::stop() {
  rumble({});
}

} // namespace cupid::desktop::linux_host
