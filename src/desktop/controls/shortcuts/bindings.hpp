#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace cupid::desktop {

enum class ShortcutAction : unsigned {
  Pause,
  Reset,
  Mute,
  Fullscreen,
  Help,
  FrameAdvance,
  SaveState,
  LoadState,
  UndoSave,
  UndoLoad,
  FastForwardToggle,
  FastForwardHold,
  RewindHold,
  Screenshot,
  MouseCapture,
  Slot1,
  Slot2,
  Slot3,
  Slot4,
  Slot5,
  Slot6,
  Slot7,
  Slot8,
  Slot9,
  Slot10,
  Count
};

enum ShortcutModifier : unsigned { ShortcutCtrl = 1, ShortcutShift = 2, ShortcutAlt = 4 };

struct ShortcutBinding {
  unsigned key = 0, modifiers = 0;
  bool operator==(const ShortcutBinding &) const = default;
};

constexpr unsigned shortcut_count = static_cast<unsigned>(ShortcutAction::Count);
using ShortcutBindings = std::array<std::array<ShortcutBinding, 2>, shortcut_count>;

struct ShortcutConflict {
  ShortcutAction first, second;
  ShortcutBinding binding;
};

ShortcutBindings default_shortcuts();
std::wstring_view shortcut_name(ShortcutAction action);
bool valid_shortcut(ShortcutBinding binding);
bool held_shortcut(ShortcutAction action);
std::optional<ShortcutConflict> shortcut_conflict(const ShortcutBindings &bindings);
void validate_shortcuts(const ShortcutBindings &bindings);

struct ShortcutEvent {
  ShortcutAction action;
  bool pressed;
};

class ShortcutRouter {
public:
  void bindings(const ShortcutBindings &bindings);
  std::optional<ShortcutEvent> key(unsigned key, unsigned modifiers, bool pressed);
  bool matches(unsigned key, unsigned modifiers) const;
  void clear();

private:
  ShortcutBindings bindings_ = default_shortcuts();
  std::array<bool, 256> down_{};
  std::array<std::optional<ShortcutAction>, 256> held_{};
};

} // namespace cupid::desktop
