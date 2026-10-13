#include "desktop/windows/window.hpp"
#include <stdexcept>

namespace cupid::desktop {
namespace {
std::chrono::nanoseconds thread_cpu_time() {
  FILETIME creation{}, exit{}, kernel{}, user{};
  if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
    return {};
  const auto ticks = [](FILETIME value) {
    return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
  };
  return std::chrono::nanoseconds((ticks(kernel) + ticks(user)) * 100);
}
} // namespace

Window::Input Window::test_input(unsigned frame) {
  const bool start = (frame >= 900 && frame < 960) || (frame >= 1200 && frame < 1260);
  const bool jump = (frame >= 1600 && frame < 1660) || (frame >= 2200 && frame < 2260) ||
                    (frame >= 2600 && frame < 2660) || (frame >= 3000 && frame < 3060) ||
                    (frame >= 3200 && frame % 300 < 60);
  return {static_cast<std::uint16_t>((start ? 0x1000 : 0) | (jump ? 0x8000 : 0)), 0,
          static_cast<std::int8_t>(frame >= 4800 ? -58 : 0)};
}

void Window::publish_inputs() {
  const auto selected = inputs();
  std::lock_guard lock(input_mutex_);
  worker_inputs_ = selected;
  for (unsigned port = 0; port < selected.size(); ++port)
    worker_mouse_[port].input(selected[port].mouse_left, selected[port].mouse_right,
                              selected[port].mouse_x, selected[port].mouse_y);
}

PlayerInputs Window::take_inputs() {
  std::lock_guard lock(input_mutex_);
  auto selected = worker_inputs_;
  for (unsigned port = 0; port < selected.size(); ++port) {
    const auto mouse = worker_mouse_[port].take();
    selected[port].mouse_x = mouse.mouse_x;
    selected[port].mouse_y = mouse.mouse_y;
  }
  return selected;
}

void Window::start_advance() {
  if (advancing_)
    return;
  publish_inputs();
  auto clock = playback_clock_;
  const auto unpaced = fast_forward_;
  auto history = rewind_enabled_ && rewind_;
  execution_.repeat([this, clock, unpaced, history]() mutable {
    if (!completed_fields_->writable())
      return false;
    const auto before = session_->frames;
    auto selected = take_inputs();
    if (input_replay_) {
      selected = {};
      selected[0] = input_replay_->at(before);
    } else if (options_.test_input) {
      const auto scripted = test_input(before);
      selected[0].buttons = scripted.buttons;
      selected[0].x = scripted.x;
      selected[0].y = scripted.y;
      selected[0].calibrated_stick = true;
    }
    const auto started =
        field_trace_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (field_trace_ && !worker_cpu_origin_)
      worker_cpu_origin_ = thread_cpu_time();
    session_->input(selected);
    const auto origin = session_->clocks();
    do {
      session_->run();
      if (session_->clocks() - origin > 10 * n64::clock_frequency)
        throw std::runtime_error("Emulation stopped because no video field was produced.");
    } while (session_->frames == before && !execution_.interrupted() &&
             session_->diagnostics().control() == n64::HostControl::None);
    if (session_->frames == before)
      return false;

    CompletedField output;
    output.completed = std::chrono::steady_clock::now();
    if (field_trace_) {
      output.duration = output.completed - started;
      output.cpu = thread_cpu_time() - *worker_cpu_origin_;
      output.processor = GetCurrentProcessorNumber();
    }
    output.observation = {session_->frames,
                          session_->clocks(),
                          session_->program_counter(),
                          audio_.generated_samples(),
                          audio_.available() && !audio_.muted && !audio_.suspended,
                          false,
                          static_cast<std::uint64_t>(audio_.wait_time().count()),
                          static_cast<std::uint64_t>(session_->renderer_time().count())};
    if (field_trace_ && field_trace_->captures(session_->frames)) {
      session_->finish_frame();
      output.capture = session_->frame;
    }
    output.image = session_->frame;
    output.feedback = feedback_state();
    output.audio_recovery = audio_.recovery_pending();
    if (history) {
      try {
        rewind_->record(session_->frames);
      } catch (...) {
        history = false;
        rewind_->clear();
        output.history_error = std::current_exception();
      }
    }
    const auto clocks = output.observation.clocks;
    completed_fields_->publish(std::move(output));
    PostMessageW(window_, WM_APP + 1, 0, 0);
    if (session_->frames % 300 == 0)
      session_->save();
    if ((options_.frames && session_->frames >= options_.frames) ||
        session_->diagnostics().control() != n64::HostControl::None)
      return false;
    if (!unpaced) {
      auto delay = clock.delay(clocks, std::chrono::steady_clock::now());
      while (delay.count()) {
        if (!execution_.wait_for(delay))
          return false;
        delay = clock.delay(clocks, std::chrono::steady_clock::now());
      }
    }
    return true;
  });
  advancing_ = true;
}

void Window::drain_advance() {
  // Bound UI work even when an unpaced producer keeps the queue occupied.
  for (unsigned consumed = 0; consumed < 3; ++consumed) {
    auto output = completed_fields_->take();
    if (!output)
      return;
    last_frame_ = output->observation.field;
    if (field_trace_) {
      output->observation.gpu_ready = video_presenter_.available();
      field_trace_->advance(output->duration, output->cpu - observed_worker_cpu_,
                            output->processor);
      observed_worker_cpu_ = output->cpu;
      field_trace_->ready(output->completed);
      field_trace_->presented(output->observation);
      if (output->capture)
        field_trace_->capture(last_frame_, *output->capture);
    }
    const auto motors = feedback_.update(output->feedback);
    gamepads_.rumble(feedback_targets(player_mappings_, motors, last_host_inputs_,
                                      session_ && !stopped() && !rewinding_));
    audio_recovery_pending_ |= output->audio_recovery;
    if (output->history_error) {
      rewind_enabled_ = false;
      try {
        std::rethrow_exception(output->history_error);
      } catch (const std::exception &error) {
        status_error(L"Rewind disabled", error);
      }
      update_playback();
    }
    refresh_frame(output->image);
    UpdateWindow(window_);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - measured_time_).count();
    if (elapsed >= 1.0) {
      const auto fps = (last_frame_ - measured_frames_) / elapsed;
      const auto title = L"Cupid-N64 | " + game_image_.filename().wstring() + L" | " +
                         std::to_wstring(static_cast<unsigned>(fps + 0.5)) + L" VI/s | Slot " +
                         std::to_wstring(state_slot_) +
                         (audio_.available() ? L"" : L" | Audio unavailable");
      SetWindowTextW(window_, title.c_str());
      measured_time_ = now;
      measured_frames_ = last_frame_;
    }
  }
}

void Window::finish_advance() {
  if (!advancing_)
    return;
  execution_.interrupt();
  std::exception_ptr failure;
  try {
    execution_.wait();
  } catch (...) {
    failure = std::current_exception();
  }
  advancing_ = false;
  drain_advance();
  update_feedback();
  apply_homebrew_control();
  if (failure)
    std::rethrow_exception(failure);
  if (options_.frames && last_frame_ >= options_.frames) {
    capture();
    quit_ = true;
  }
}

} // namespace cupid::desktop
