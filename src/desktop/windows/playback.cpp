#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {

void Window::update_playback() {
  EnableMenuItem(menu_, MemoryInspection, MF_BYCOMMAND | (session_ ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(menu_, DeviceProperties, MF_BYCOMMAND | (session_ ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(menu_, CoreDiagnostics, MF_BYCOMMAND | (session_ ? MF_ENABLED : MF_GRAYED));
  update_shortcuts();
  CheckMenuItem(menu_, Pause, MF_BYCOMMAND | (paused_ ? MF_CHECKED : MF_UNCHECKED));
  EnableMenuItem(
      menu_, FrameAdvance,
      MF_BYCOMMAND |
          (session_ && paused_ && !session_->debugger().attached() ? MF_ENABLED : MF_GRAYED));
  CheckMenuItem(menu_, FastForward,
                MF_BYCOMMAND | (fast_forward_toggle_ ? MF_CHECKED : MF_UNCHECKED));
  EnableMenuItem(menu_, FastForward,
                 MF_BYCOMMAND |
                     (session_ && !paused_ && active_ && !rewinding_ ? MF_ENABLED : MF_GRAYED));
  const bool muted = rewinding_ ? rewind_was_muted_ : audio_.muted;
  CheckMenuItem(menu_, Mute, MF_BYCOMMAND | (muted ? MF_CHECKED : MF_UNCHECKED));
  if (rewind_menu_) {
    CheckMenuItem(rewind_menu_, RewindEnabled,
                  MF_BYCOMMAND | (rewind_enabled_ ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(rewind_menu_, RewindMute,
                  MF_BYCOMMAND | (rewind_mute_ ? MF_CHECKED : MF_UNCHECKED));
    const auto choice = [&](unsigned first, const auto &values, unsigned selected) {
      for (unsigned n = 0; n < values.size(); ++n) {
        CheckMenuItem(rewind_menu_, first + n,
                      MF_BYCOMMAND | (values[n] == selected ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(rewind_menu_, first + n,
                       MF_BYCOMMAND | (rewind_enabled_ ? MF_ENABLED : MF_GRAYED));
      }
    };
    choice(RewindIntervalFirst, rewind_intervals, rewind_settings_.interval);
    choice(RewindCapacityFirst, rewind_capacities, rewind_settings_.capacity);
    choice(RewindMemoryFirst, rewind_memory,
           static_cast<unsigned>(rewind_settings_.memory / (1024 * 1024)));
    EnableMenuItem(rewind_menu_, RewindMute,
                   MF_BYCOMMAND | (rewind_enabled_ ? MF_ENABLED : MF_GRAYED));
  }
  DrawMenuBar(window_);
  InvalidateRect(window_, nullptr, FALSE);
}

void Window::update_fast_forward() {
  const bool enabled = session_ && !session_->debugger().attached() && !stopped() && !rewinding_ &&
                       (fast_forward_hold_ || fast_forward_toggle_);
  if (enabled != fast_forward_) {
    fast_forward_ = enabled;
    auto audio = audio_.settings();
    audio.blocking = playback_sync_.audio && !enabled;
    audio_.configure(std::move(audio));
    rebase();
  }
  update_playback();
}

void Window::stop_fast_forward() {
  fast_forward_hold_ = fast_forward_toggle_ = false;
  update_fast_forward();
}

bool Window::apply_homebrew_control() {
  const auto control = session_->diagnostics().take_control();
  if (control == n64::HostControl::None)
    return false;
  if (control == n64::HostControl::Exit)
    quit_ = true;
  else {
    fast_forward_hold_ = false;
    fast_forward_toggle_ = control == n64::HostControl::FastForward;
    update_fast_forward();
  }
  return true;
}

bool Window::playback_command(unsigned id) {
  if (id == FastForward) {
    if (session_ && !paused_ && active_ && !rewinding_)
      fast_forward_toggle_ = !fast_forward_toggle_;
    update_fast_forward();
    return true;
  }
  if (id == HoldFastForward || id == ReleaseFastForward) {
    fast_forward_hold_ = id == HoldFastForward && session_ && !stopped() && !rewinding_;
    update_fast_forward();
    return true;
  }
  if (id == BeginRewind || id == EndRewind) {
    if (id == BeginRewind)
      begin_rewind();
    else
      end_rewind();
    return true;
  }
  if (id == Mute && rewinding_) {
    rewind_was_muted_ = !rewind_was_muted_;
    audio_.muted = rewind_mute_ || rewind_was_muted_;
    audio_.clear();
    update_playback();
    return true;
  }
  const bool interval =
      id >= RewindIntervalFirst && id < RewindIntervalFirst + rewind_intervals.size();
  const bool capacity =
      id >= RewindCapacityFirst && id < RewindCapacityFirst + rewind_capacities.size();
  const bool memory = id >= RewindMemoryFirst && id < RewindMemoryFirst + rewind_memory.size();
  if (id != RewindEnabled && id != RewindMute && !interval && !capacity && !memory)
    return false;
  end_rewind();
  if (id == RewindEnabled)
    rewind_enabled_ = !rewind_enabled_;
  if (id == RewindMute)
    rewind_mute_ = !rewind_mute_;
  if (interval)
    rewind_settings_.interval = rewind_intervals[id - RewindIntervalFirst];
  if (capacity)
    rewind_settings_.capacity = rewind_capacities[id - RewindCapacityFirst];
  if (memory)
    rewind_settings_.memory = std::size_t(rewind_memory[id - RewindMemoryFirst]) * 1024 * 1024;
  if (rewind_) {
    rewind_->configure(rewind_settings_);
    if (id == RewindEnabled)
      rewind_->clear();
  }
  if (!settings_.empty() && !options_.frames) {
    const auto write = [&](const wchar_t *key, unsigned value) {
      WritePrivateProfileStringW(L"Rewind", key, std::to_wstring(value).c_str(), settings_.c_str());
    };
    write(L"Enabled", rewind_enabled_);
    write(L"Mute", rewind_mute_);
    write(L"Interval", rewind_settings_.interval);
    write(L"Capacity", rewind_settings_.capacity);
    write(L"MemoryMB", static_cast<unsigned>(rewind_settings_.memory / (1024 * 1024)));
  }
  rebase();
  update_playback();
  return true;
}

void Window::record_history() {
  if (!rewind_enabled_ || !rewind_ || rewinding_)
    return;
  try {
    rewind_->record(session_->frames);
  } catch (const std::exception &error) {
    rewind_enabled_ = false;
    rewind_->clear();
    status_error(L"Rewind disabled", error);
    update_playback();
  }
}

void Window::begin_rewind() {
  if (!rewind_enabled_ || !session_ || session_->debugger().attached() || !rewind_ || stopped() ||
      rewinding_)
    return;
  if (!rewind_->can_rewind(session_->frames)) {
    state_message_ = L"No earlier rewind snapshot is available.";
    InvalidateRect(window_, nullptr, FALSE);
    return;
  }
  stop_fast_forward();
  gamepads_.stop();
  release_mouse();
  rewind_was_muted_ = audio_.muted;
  audio_.clear();
  audio_.muted = rewind_mute_ || rewind_was_muted_;
  rewinding_ = true;
  next_rewind_ = std::chrono::steady_clock::now();
  update_playback();
}

void Window::rewind_step() {
  if (!rewinding_ || !rewind_ || !session_)
    return;
  try {
    if (rewind_->rewind(session_->frames)) {
      session_->finish_frame();
      frame_ready_ = false;
      last_frame_ = session_->frames;
      refresh_frame();
      audio_.play_saved_output();
      state_message_ = L"Rewinding | " + std::to_wstring(rewind_->size()) + L" snapshots retained";
      rebase();
    } else {
      state_message_ = L"Rewind history exhausted. Release the rewind shortcut to resume.";
      InvalidateRect(window_, nullptr, FALSE);
    }
  } catch (const std::exception &error) {
    end_rewind();
    rewind_enabled_ = false;
    rewind_->clear();
    status_error(L"Rewind disabled", error);
    update_playback();
  }
  next_rewind_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
}

void Window::end_rewind() {
  if (!rewinding_)
    return;
  rewinding_ = false;
  audio_.muted = rewind_was_muted_;
  audio_.clear();
  clear_host_input();
  state_message_ = L"Resumed from rewind point";
  rebase();
  update_playback();
}

} // namespace cupid::desktop
