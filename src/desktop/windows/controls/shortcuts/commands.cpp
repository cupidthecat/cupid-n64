#include "desktop/windows/commands.hpp"
#include "desktop/windows/controls/keys.hpp"
#include "desktop/windows/controls/shortcuts/dialog.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {

void Window::clear_host_input() {
  keys_.fill(false);
  shortcuts_.clear();
  shortcut_alt_ = false;
  release_mouse();
}

bool Window::guest_key(unsigned key) const {
  const auto matches = [&](InputBinding binding) {
    if (binding.kind != BindingKind::Key)
      return false;
    if (binding.code == key)
      return true;
    return (key == VK_SHIFT && (binding.code == VK_LSHIFT || binding.code == VK_RSHIFT)) ||
           (key == VK_CONTROL && (binding.code == VK_LCONTROL || binding.code == VK_RCONTROL)) ||
           (key == VK_MENU && (binding.code == VK_LMENU || binding.code == VK_RMENU));
  };
  for (unsigned port = 0; port < player_mappings_.size(); ++port) {
    if (player_mappings_[port].source != HostSource::Keyboard ||
        (session_ && session_->port_status(port).controller == ControllerType::Mouse))
      continue;
    const auto &mapping = player_mappings_[port];
    for (const auto binding : mapping.buttons)
      if (matches(binding))
        return true;
    for (const auto axis : mapping.axes)
      if (matches(axis.negative) || matches(axis.positive))
        return true;
    if (matches(mapping.slow))
      return true;
  }
  return false;
}

bool Window::shortcut_key(unsigned key, bool pressed, bool repeat) {
  unsigned modifiers = 0;
  if (keys_[VK_CONTROL] || (GetKeyState(VK_CONTROL) & 0x8000))
    modifiers |= ShortcutCtrl;
  if (keys_[VK_SHIFT] || (GetKeyState(VK_SHIFT) & 0x8000))
    modifiers |= ShortcutShift;
  if (keys_[VK_MENU] || (GetKeyState(VK_MENU) & 0x8000))
    modifiers |= ShortcutAlt;
  if (pressed && key == VK_ESCAPE && mouse_.captured()) {
    if (!repeat)
      release_mouse();
    return true;
  }
  const bool matched = shortcuts_.matches(key, modifiers);
  const auto event = pressed && repeat ? std::nullopt : shortcuts_.key(key, modifiers, pressed);
  if (event)
    shortcut(*event);
  if (pressed && matched && (modifiers & ShortcutAlt))
    shortcut_alt_ = true;
  if (key == VK_MENU && !pressed && shortcut_alt_) {
    shortcut_alt_ = false;
    return true;
  }
  return matched || event.has_value() || guest_key(key);
}

void Window::shortcut(ShortcutEvent event) {
  const auto action = event.action;
  if (action == ShortcutAction::FastForwardHold) {
    command(event.pressed ? HoldFastForward : ReleaseFastForward);
    return;
  }
  if (action == ShortcutAction::RewindHold) {
    command(event.pressed ? BeginRewind : EndRewind);
    return;
  }
  if (!event.pressed)
    return;
  if (action >= ShortcutAction::Slot1 && action <= ShortcutAction::Slot10) {
    command(StateSlotFirst + static_cast<unsigned>(action) -
            static_cast<unsigned>(ShortcutAction::Slot1));
    return;
  }
  constexpr std::array<unsigned, 15> commands{
      Pause,        Reset,     Mute,      Fullscreen,    Controls,
      FrameAdvance, SaveState, LoadState, UndoStateSave, UndoStateLoad,
      FastForward,  0,         0,         Screenshot,    MouseCapture};
  const auto index = static_cast<unsigned>(action);
  if (index < commands.size() && commands[index])
    command(commands[index]);
}

std::wstring Window::shortcut_label(ShortcutAction action, std::wstring label) const {
  const auto text = shortcut_text(shortcut_bindings_[static_cast<unsigned>(action)]);
  return text.empty() ? label : std::move(label) + L"\t" + text;
}

void Window::update_shortcuts() {
  if (!menu_)
    return;
  const auto label = [&](unsigned id, const std::wstring &text) {
    const auto state = GetMenuState(menu_, id, MF_BYCOMMAND);
    if (state != UINT(-1))
      ModifyMenuW(menu_, id,
                  MF_BYCOMMAND | MF_STRING | (state & (MF_CHECKED | MF_DISABLED | MF_GRAYED)), id,
                  text.c_str());
  };
  label(Pause, shortcut_label(ShortcutAction::Pause, L"&Pause"));
  label(Reset, shortcut_label(ShortcutAction::Reset, L"&Reset"));
  label(Mute, shortcut_label(ShortcutAction::Mute, L"&Mute"));
  label(Fullscreen, shortcut_label(ShortcutAction::Fullscreen, L"&Fullscreen"));
  label(Controls, shortcut_label(ShortcutAction::Help, L"&Controls / help"));
  label(FrameAdvance, shortcut_label(ShortcutAction::FrameAdvance, L"Frame &advance"));
  label(SaveState, shortcut_label(ShortcutAction::SaveState, L"&Save state"));
  label(LoadState, shortcut_label(ShortcutAction::LoadState, L"&Load state"));
  label(UndoStateLoad, shortcut_label(ShortcutAction::UndoLoad, L"Undo l&oad"));
  label(Screenshot, shortcut_label(ShortcutAction::Screenshot, L"&Screenshot..."));
  auto forward = shortcut_label(ShortcutAction::FastForwardToggle, L"&Fast-forward");
  const auto hold =
      shortcut_text(shortcut_bindings_[static_cast<unsigned>(ShortcutAction::FastForwardHold)]);
  if (!hold.empty())
    forward += (forward.find(L'\t') == std::wstring::npos ? L"\thold " : L" / hold ") + hold;
  label(FastForward, forward);
  const auto rewind =
      shortcut_text(shortcut_bindings_[static_cast<unsigned>(ShortcutAction::RewindHold)]);
  label(RewindHint,
        rewind.empty() ? L"Rewind shortcut unassigned" : L"Hold " + rewind + L" to rewind");
  auto pointer =
      shortcut_label(ShortcutAction::MouseCapture,
                     mouse_.captured() ? L"&Release mouse pointer" : L"&Capture mouse pointer");
  if (mouse_.captured())
    pointer +=
        (pointer.find(L'\t') == std::wstring::npos ? L"\t" : L" / ") + std::wstring(L"Escape");
  label(MouseCapture, pointer);
  EnableMenuItem(menu_, MouseCapture,
                 MF_BYCOMMAND |
                     (mouse_.captured() || (has_mouse() && !stopped() && !rewinding_) ? MF_ENABLED
                                                                                      : MF_GRAYED));
  DrawMenuBar(window_);
}

void Window::shortcuts_dialog() {
  if (auto selected = choose_shortcuts(window_, shortcut_bindings_);
      selected && *selected != shortcut_bindings_) {
    if (!options_.frames)
      save_shortcuts(settings_, *selected);
    shortcut_bindings_ = std::move(*selected);
    shortcuts_.bindings(shortcut_bindings_);
    state_message_ = L"Shortcuts saved";
  }
  update_shortcuts();
  update_states();
}

bool Window::input_tools_command(unsigned id) {
  if (id == MouseCapture) {
    mouse_capture();
    return true;
  }
  if (id != Shortcuts)
    return false;
  end_rewind();
  stop_fast_forward();
  gamepads_.stop();
  clear_host_input();
  try {
    shortcuts_dialog();
  } catch (const std::exception &error) {
    status_error(L"Shortcut settings failed", error);
  }
  rebase();
  return true;
}

} // namespace cupid::desktop
