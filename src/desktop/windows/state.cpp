#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"
#include <algorithm>
#include <string_view>

namespace cupid::desktop {
namespace {

std::wstring error_text(const std::exception &error) {
  const std::string_view text(error.what());
  const auto size = static_cast<int>(text.size());
  const auto length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
  if (!length)
    return L"The save-state operation failed.";
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), length);
  return result;
}

} // namespace

void Window::update_states() {
  const auto occupied = [&](unsigned slot) {
    try {
      return states_ && states_->occupied(slot);
    } catch (const std::filesystem::filesystem_error &) {
      return false;
    }
  };
  const auto available = [&](unsigned command, bool enabled) {
    EnableMenuItem(state_menu_, command, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
  };
  available(SaveState, bool(states_));
  available(LoadState, occupied(state_slot_));
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
                       (occupied(slot) ? L" (saved)" : L" (empty)") + L"\tCtrl+" +
                       std::to_wstring(slot % 10);
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
  try {
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
  } catch (const std::exception &error) {
    status_error(L"State action failed", error);
  }
  // Snapshot transfers and disk writes can outlast the normal frame deadline.
  rebase();
  update_states();
  return true;
}

void Window::state_loaded() {
  gamepads_.stop();
  feedback_.clear();
  if (rewinding_)
    audio_.muted = rewind_was_muted_;
  rewinding_ = false;
  fast_forward_ = fast_forward_hold_ = fast_forward_toggle_ = false;
  audio_.suspended = false;
  if (rewind_)
    rewind_->clear();
  if (paused_)
    session_->finish_frame();
  frame_ready_ = false;
  last_frame_ = session_->frames;
  keys_.fill(false);
  refresh_frame();
  update_playback();
}

void Window::status_error(std::wstring_view action, const std::exception &error) {
  state_message_ = std::wstring(action) + L": " + error_text(error);
  InvalidateRect(window_, nullptr, FALSE);
}

void Window::refresh_frame() {
  const bool available = !pixels_.empty();
  pixels_ = session_->frame.rgba;
  pixel_width_ = session_->frame.width;
  pixel_height_ = session_->frame.height;
  for (std::size_t offset = 0; offset < pixels_.size(); offset += 4)
    std::swap(pixels_[offset], pixels_[offset + 2]);
  if (available != !pixels_.empty())
    update_display();
  InvalidateRect(window_, nullptr, FALSE);
}

} // namespace cupid::desktop
