#include "desktop/controls/shortcuts/bindings.hpp"
#include <stdexcept>

namespace cupid::desktop {

ShortcutBindings default_shortcuts() {
  ShortcutBindings bindings{};
  const auto bind = [&](ShortcutAction action, unsigned key, unsigned modifiers = 0) {
    bindings[static_cast<unsigned>(action)][0] = {key, modifiers};
  };
  bind(ShortcutAction::Pause, 0x74);
  bindings[static_cast<unsigned>(ShortcutAction::Pause)][1] = {0x1b, 0};
  bind(ShortcutAction::Reset, 0x75);
  bind(ShortcutAction::Mute, 0x77);
  bind(ShortcutAction::Fullscreen, 0x7a);
  bind(ShortcutAction::Help, 0x70);
  bind(ShortcutAction::FrameAdvance, 0x73);
  bind(ShortcutAction::SaveState, 0x71);
  bind(ShortcutAction::LoadState, 0x72);
  bind(ShortcutAction::UndoSave, 0x71, ShortcutCtrl);
  bind(ShortcutAction::UndoLoad, 0x72, ShortcutCtrl);
  bind(ShortcutAction::FastForwardToggle, 0x78);
  bind(ShortcutAction::FastForwardHold, 0x09);
  bind(ShortcutAction::RewindHold, 0x76);
  bind(ShortcutAction::Screenshot, 0x7b);
  bind(ShortcutAction::MouseCapture, 'M', ShortcutCtrl);
  for (unsigned slot = 0; slot < 10; ++slot)
    bind(static_cast<ShortcutAction>(static_cast<unsigned>(ShortcutAction::Slot1) + slot),
         slot == 9 ? '0' : '1' + slot, ShortcutCtrl);
  return bindings;
}

std::wstring_view shortcut_name(ShortcutAction action) {
  constexpr std::array<std::wstring_view, shortcut_count> names{L"Pause",
                                                                L"Reset",
                                                                L"Mute",
                                                                L"Fullscreen",
                                                                L"Help",
                                                                L"Frame advance",
                                                                L"Save state",
                                                                L"Load state",
                                                                L"Undo save",
                                                                L"Undo load",
                                                                L"Toggle fast-forward",
                                                                L"Hold fast-forward",
                                                                L"Hold rewind",
                                                                L"Screenshot",
                                                                L"Capture / release mouse",
                                                                L"State slot 1",
                                                                L"State slot 2",
                                                                L"State slot 3",
                                                                L"State slot 4",
                                                                L"State slot 5",
                                                                L"State slot 6",
                                                                L"State slot 7",
                                                                L"State slot 8",
                                                                L"State slot 9",
                                                                L"State slot 10"};
  const auto index = static_cast<unsigned>(action);
  return index < names.size() ? names[index] : std::wstring_view{};
}

bool valid_shortcut(ShortcutBinding binding) {
  if (!binding.key)
    return !binding.modifiers;
  return binding.key >= 8 && binding.key < 256 && binding.modifiers < 8 && binding.key != 0x10 &&
         binding.key != 0x11 && binding.key != 0x12 && binding.key != 0x5b && binding.key != 0x5c &&
         (binding.key < 0xa0 || binding.key > 0xa5);
}

bool held_shortcut(ShortcutAction action) {
  return action == ShortcutAction::FastForwardHold || action == ShortcutAction::RewindHold;
}

std::optional<ShortcutConflict> shortcut_conflict(const ShortcutBindings &bindings) {
  for (unsigned first = 0; first < bindings.size() * 2; ++first) {
    const auto binding = bindings[first / 2][first % 2];
    if (!binding.key)
      continue;
    for (unsigned second = first + 1; second < bindings.size() * 2; ++second)
      if (binding == bindings[second / 2][second % 2])
        return ShortcutConflict{static_cast<ShortcutAction>(first / 2),
                                static_cast<ShortcutAction>(second / 2), binding};
  }
  return std::nullopt;
}

void validate_shortcuts(const ShortcutBindings &bindings) {
  for (const auto &action : bindings)
    for (const auto binding : action)
      if (!valid_shortcut(binding))
        throw std::runtime_error("Select a valid key and modifier combination.");
  if (shortcut_conflict(bindings))
    throw std::runtime_error("Two shortcuts use the same key and modifiers.");
}

void ShortcutRouter::bindings(const ShortcutBindings &bindings) {
  validate_shortcuts(bindings);
  bindings_ = bindings;
  clear();
}

bool ShortcutRouter::matches(unsigned key, unsigned modifiers) const {
  if (!key)
    return false;
  for (const auto &action : bindings_)
    for (const auto binding : action)
      if (binding == ShortcutBinding{key, modifiers})
        return true;
  return false;
}

std::optional<ShortcutEvent> ShortcutRouter::key(unsigned key, unsigned modifiers, bool pressed) {
  if (key >= down_.size())
    return std::nullopt;
  if (!pressed) {
    down_[key] = false;
    const auto action = held_[key];
    held_[key].reset();
    if (action) {
      for (const auto &held : held_)
        if (held == action)
          return std::nullopt;
      return ShortcutEvent{*action, false};
    }
    return std::nullopt;
  }
  if (down_[key])
    return std::nullopt;
  down_[key] = true;
  for (unsigned action = 0; action < bindings_.size(); ++action)
    for (const auto binding : bindings_[action])
      if (binding.key && binding == ShortcutBinding{key, modifiers}) {
        const auto selected = static_cast<ShortcutAction>(action);
        if (held_shortcut(selected)) {
          bool already_held = false;
          for (const auto &held : held_)
            already_held |= held == selected;
          held_[key] = selected;
          if (already_held)
            return std::nullopt;
        }
        return ShortcutEvent{selected, true};
      }
  return std::nullopt;
}

void ShortcutRouter::clear() {
  down_.fill(false);
  held_.fill(std::nullopt);
}

} // namespace cupid::desktop
