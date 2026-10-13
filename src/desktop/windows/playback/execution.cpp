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
  presentation_.quiesce();
  const auto surface = video_surface();
  video_presenter_.prepare(surface);
  video_presenter_.show(video_available_);
  session_->connect_present([this, surface](unsigned field, const n64::VideoFrame &frame) {
    if (options_.frames && field > options_.frames)
      return;
    const auto started = std::chrono::steady_clock::now();
    FrameDelivery<CompletedField>::Presented output;
    FrameDelivery<CompletedField>::Image image;
    output.image_valid = frame.width && frame.height && frame.width <= n64::maximum_frame_width &&
                         frame.height <= n64::maximum_frame_height &&
                         frame.rgba.size() == std::size_t(frame.width) * frame.height * 4;
    try {
      if ((field_trace_ && field_trace_->captures(field)) || field == options_.frames)
        output.capture = frame;
      const auto shown = shader_.apply(frame);
      image.pixels.assign(shown.begin(), shown.end());
      image.width = frame.width;
      image.height = frame.height;
      output.completion.result.width = frame.width;
      output.completion.result.height = frame.height;
      for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4)
        std::swap(image.pixels[offset], image.pixels[offset + 2]);
      output.shader_failed = !shader_.error().empty();
      output.completion.result.presented =
          output.image_valid &&
          video_presenter_.render(image.pixels, image.width, image.height, surface);
      output.completion.result.available = video_presenter_.available();
      output.completion.result.error = video_presenter_.error();
    } catch (...) {
      output.completion.failure = std::current_exception();
    }
    output.completion.completed = std::chrono::steady_clock::now();
    output.completion.duration = output.completion.completed - started;
    const auto failure = output.completion.failure;
    completed_fields_.presented(field, std::move(output), std::move(image));
    PostMessageW(window_, WM_APP + 1, 0, 0);
    if (failure)
      std::rethrow_exception(failure);
  });
  struct Measurement {
    std::chrono::steady_clock::time_point started;
    std::chrono::nanoseconds duration{};
    bool history;
    std::vector<CompletedField> ready;
  };
  auto measurement = std::make_shared<Measurement>();
  measurement->history = rewind_enabled_ && rewind_;
  session_->observe_frames([this, measurement](unsigned field) {
    if (options_.frames && field > options_.frames)
      return;
    CompletedField output;
    output.completed = std::chrono::steady_clock::now();
    if (field_trace_) {
      measurement->duration += output.completed - measurement->started;
      measurement->started = output.completed;
      output.duration = std::exchange(measurement->duration, {});
      output.cpu = thread_cpu_time() - *worker_cpu_origin_;
      output.processor = GetCurrentProcessorNumber();
    }
    output.observation.field = field;
    measurement->ready.push_back(std::move(output));
  });
  advancing_ = true;
  try {
    execution_.repeat([this, clock = playback_clock_, unpaced = fast_forward_,
                       measurement]() mutable {
      const auto pace = [&](std::uint64_t clocks) {
        if (!unpaced) {
          auto delay = clock.delay(clocks, std::chrono::steady_clock::now());
          while (delay.count()) {
            if (!execution_.wait_for(delay))
              return false;
            delay = clock.delay(clocks, std::chrono::steady_clock::now());
          }
        }
        return true;
      };
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
      measurement->started =
          field_trace_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
      if (field_trace_ && !worker_cpu_origin_)
        worker_cpu_origin_ = thread_cpu_time();
      session_->input(selected);
      session_->clear_refresh();
      const auto origin = session_->clocks();
      const auto publish = [&] {
        if (measurement->ready.empty())
          return;
        if (measurement->history) {
          try {
            rewind_->record(session_->frames);
          } catch (...) {
            measurement->history = false;
            rewind_->clear();
            measurement->ready.back().history_error = std::current_exception();
          }
        }
        for (auto &output : measurement->ready) {
          const auto field = output.observation.field;
          output.observation = {field,
                                session_->clocks(),
                                session_->program_counter(),
                                audio_.generated_samples(),
                                audio_.available() && !audio_.muted && !audio_.suspended,
                                false,
                                static_cast<std::uint64_t>(audio_.wait_time().count()),
                                static_cast<std::uint64_t>(session_->renderer_time().count())};
          output.feedback = feedback_state();
          output.audio_recovery = audio_.recovery_pending();
          completed_fields_.metadata(field, std::move(output));
        }
        measurement->ready.clear();
        PostMessageW(window_, WM_APP + 1, 0, 0);
      };
      do {
        try {
          session_->run();
        } catch (...) {
          const auto failure = std::current_exception();
          publish();
          std::rethrow_exception(failure);
        }
        // Metadata observes the complete ordinary interval, including audio and events.
        publish();
        if (!session_->refreshed() && session_->clocks() - origin > 10 * n64::clock_frequency)
          throw std::runtime_error("Emulation stopped because no video field was produced.");
      } while (!session_->refreshed() && session_->frames == before && !execution_.interrupted() &&
               session_->diagnostics().control() == n64::HostControl::None);
      if (field_trace_)
        measurement->duration += std::chrono::steady_clock::now() - measurement->started;
      if (session_->frames == before) {
        if (!session_->refreshed() || session_->diagnostics().control() != n64::HostControl::None)
          return false;
        return pace(session_->clocks());
      }

      if (session_->frames % 300 == 0)
        session_->save();
      if ((options_.frames && session_->frames >= options_.frames) ||
          session_->diagnostics().control() != n64::HostControl::None)
        return false;
      return pace(session_->clocks());
    });
  } catch (...) {
    advancing_ = false;
    session_->observe_frames({});
    session_->connect_present({});
    throw;
  }
}

bool Window::drain_advance() {
  bool accepted = false;
  // Bound UI work even when an unpaced producer keeps the queue occupied.
  for (unsigned consumed = 0; consumed < 3; ++consumed) {
    presentation_.poll();
    if (presentation_.pending())
      return accepted;
    auto delivered = completed_fields_.take();
    if (!delivered) {
      refresh_presented_image();
      return accepted;
    }
    auto &output = delivered->metadata;
    auto &presented = delivered->presentation;
    const auto &completion = presented.completion;
    refresh_presented_image();
    output.observation.image_valid = presented.image_valid;
    if (presented.shader_failed != !shader_message_.empty())
      update_shader(presented.shader_failed);
    last_frame_ = output.observation.field;
    if (field_trace_) {
      field_trace_->advance(output.duration, output.cpu - observed_worker_cpu_, output.processor);
      observed_worker_cpu_ = output.cpu;
      field_trace_->ready(output.completed);
      if (presented.capture && field_trace_->captures(last_frame_))
        field_trace_->capture(last_frame_, *presented.capture);
    }
    if (last_frame_ == options_.frames)
      target_capture_ = std::move(presented.capture);
    const auto motors = feedback_.update(output.feedback);
    gamepads_.rumble(feedback_targets(player_mappings_, motors, last_host_inputs_,
                                      session_ && !stopped() && !rewinding_));
    audio_recovery_pending_ |= output.audio_recovery;
    observe_presentation(output.observation, completion);
    if (output.history_error) {
      rewind_enabled_ = false;
      try {
        std::rethrow_exception(output.history_error);
      } catch (const std::exception &error) {
        status_error(L"Rewind disabled", error);
      }
      update_playback();
    }
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
    if (completion.failure)
      std::rethrow_exception(completion.failure);
    accepted = true;
  }
  return accepted;
}

void Window::finish_advance() {
  if (finishing_advance_)
    return;
  finishing_advance_ = true;
  try {
    const bool advancing = advancing_;
    std::exception_ptr failure;
    if (advancing) {
      execution_.interrupt();
      try {
        execution_.wait();
      } catch (...) {
        failure = std::current_exception();
      }
      try {
        session_->wait_presented();
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
      session_->observe_frames({});
      try {
        session_->connect_present({});
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
      advancing_ = false;
    }
    for (;;) {
      try {
        presentation_.quiesce();
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
      try {
        if (!drain_advance())
          break;
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
    }
    if (advancing) {
      update_feedback();
      apply_homebrew_control();
    }
    if (failure)
      std::rethrow_exception(failure);
    if (advancing && session_ && options_.frames && last_frame_ >= options_.frames) {
      capture();
      quit_ = true;
    }
  } catch (...) {
    finishing_advance_ = false;
    throw;
  }
  finishing_advance_ = false;
}

} // namespace cupid::desktop
