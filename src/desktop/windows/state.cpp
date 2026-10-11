#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"
#include <algorithm>

namespace cupid::desktop {

void Window::update_states() {
  const auto available = [&](unsigned command, bool enabled) {
    EnableMenuItem(state_menu_, command, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
  };
  available(SaveState, bool(states_));
  available(LoadState, states_ && states_->occupied(state_slot_));
  const auto previous_slot = states_ ? states_->undo_save_slot() : std::nullopt;
  available(UndoStateLoad, states_ && states_->can_undo_load());
  const auto undo_label =
      previous_slot ? L"Undo save to slot " + std::to_wstring(*previous_slot) + L"\tCtrl+F2"
                    : L"Undo save\tCtrl+F2";
  ModifyMenuW(state_menu_, UndoStateSave, MF_BYCOMMAND | MF_STRING, UndoStateSave,
              undo_label.c_str());
  available(UndoStateSave, previous_slot.has_value());
  for (unsigned slot = 1; slot <= StateSlots::count; ++slot) {
    const auto label = L"Slot " + std::to_wstring(slot) +
                       (states_ && states_->occupied(slot) ? L" (saved)" : L" (empty)") +
                       L"\tCtrl+" + std::to_wstring(slot % 10);
    ModifyMenuW(state_menu_, StateSlotFirst + slot - 1,
                MF_BYCOMMAND | MF_STRING | (slot == state_slot_ ? MF_CHECKED : MF_UNCHECKED),
                StateSlotFirst + slot - 1, label.c_str());
    available(StateSlotFirst + slot - 1, bool(states_));
  }
  if (states_) {
    const auto title = L"Cupid-N64 | " + game_image_.filename().wstring() + L" | Slot " +
                       std::to_wstring(state_slot_);
    SetWindowTextW(window_, title.c_str());
  }
  DrawMenuBar(window_);
  InvalidateRect(window_, nullptr, FALSE);
}

bool Window::state_command(unsigned id) {
  const bool selecting = id >= StateSlotFirst && id < StateSlotFirst + StateSlots::count;
  if (!selecting && id != SaveState && id != LoadState && id != UndoStateSave &&
      id != UndoStateLoad)
    return false;
  if (!states_)
    return true;
  if (selecting) {
    state_slot_ = id - StateSlotFirst + 1;
    states_->select(state_slot_);
    if (!settings_.empty() && !options_.frames)
      WritePrivateProfileStringW(L"States", L"Slot", std::to_wstring(state_slot_).c_str(),
                                 settings_.c_str());
  } else if (id == SaveState) {
    states_->save();
    state_message_ = L"Saved slot " + std::to_wstring(state_slot_);
  } else if (id == LoadState && states_->occupied(state_slot_)) {
    states_->load();
    state_loaded();
    state_message_ = L"Loaded slot " + std::to_wstring(state_slot_);
  } else if (id == UndoStateSave && states_->undo_save_slot()) {
    const auto slot = *states_->undo_save_slot();
    states_->undo_save();
    state_message_ = L"Undid save to slot " + std::to_wstring(slot);
  } else if (id == UndoStateLoad && states_->can_undo_load()) {
    states_->undo_load();
    state_loaded();
    state_message_ = L"Undid state load";
  }
  // Snapshot transfers and disk writes can outlast the normal frame deadline.
  rebase();
  update_states();
  return true;
}

void Window::state_loaded() {
  frame_ready_ = false;
  last_frame_ = session_->frames;
  keys_.fill(false);
  refresh_frame();
}

void Window::refresh_frame() {
  pixels_ = session_->frame.rgba;
  pixel_width_ = session_->frame.width;
  pixel_height_ = session_->frame.height;
  for (std::size_t offset = 0; offset < pixels_.size(); offset += 4)
    std::swap(pixels_[offset], pixels_[offset + 2]);
  InvalidateRect(window_, nullptr, FALSE);
}

} // namespace cupid::desktop
