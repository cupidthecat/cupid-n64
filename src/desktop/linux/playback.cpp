#include "desktop/linux/window.hpp"
#include <gdk/gdkx.h>
#undef None
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop::linux_host {

void Window::cache_session() {
  if (!session_)
    return;
  observed_frames_ = session_->frames;
  hardware_rendering_ = session_->hardware_rendering();
  debugger_attached_ = session_->debugger().attached();
  debugger_halted_ = session_->debugger().halted();
  for (unsigned port = 0; port < controllers_.size(); ++port) {
    controllers_[port] = session_->port_status(port).controller;
    feedback_state_[port] = session_->feedback_state(port);
  }
}

void Window::refresh_frame(const n64::VideoFrame &frame) {
  if (!frame.width || !frame.height || frame.rgba.empty())
    return;
  pixels_ = frame.rgba;
  for (std::size_t n = 0; n < pixels_.size(); n += 4)
    std::swap(pixels_[n], pixels_[n + 2]);
  frame_width_ = frame.width;
  frame_height_ = frame.height;
}

VideoSurface Window::video_surface() const {
  const auto factor = drawing_ ? gtk_widget_get_scale_factor(drawing_) : 1;
  const auto width = drawing_ ? gtk_widget_get_allocated_width(drawing_) * factor : 1;
  const auto height = drawing_ ? gtk_widget_get_allocated_height(drawing_) * factor : 1;
  return {static_cast<unsigned>(std::max(1, width)), static_cast<unsigned>(std::max(1, height)),
          settings_.scale, settings_.aspect, settings_.sync.video};
}

void Window::initialize_video() {
  finish_playback();
  if (!drawing_)
    return;
  auto window = gtk_widget_get_window(drawing_);
  if (!window || !GDK_IS_X11_DISPLAY(gdk_window_get_display(window)) ||
      !gdk_window_ensure_native(window))
    return;
  gdk_display_sync(gdk_window_get_display(window));
  video_available_ = video_.initialize(gdk_x11_window_get_xid(window));
  if (video_available_)
    video_.prepare(video_surface());
}

void Window::refresh_presented_image() {
  auto image = completed_fields_.take_image();
  if (!image)
    return;
  pixels_ = std::move(image->pixels);
  frame_width_ = image->width;
  frame_height_ = image->height;
}

void Window::publish_inputs() {
  const auto selected = inputs(false);
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

void Window::start_playback() {
  if (advancing_ || !session_ || stopped() || failed_ || gdb_->port() || debugger_attached_)
    return;
  cache_session();
  publish_inputs();
  worker_control_ = n64::HostControl::None;
  const auto surface = video_surface();
  if (video_available_) {
    video_.prepare(surface);
    video_.show(true);
  }
  session_->connect_present([this, surface](unsigned field, const n64::VideoFrame &frame) {
    if (options_.frames && field > options_.frames)
      return;
    FrameDelivery<CompletedField>::Presented output;
    FrameDelivery<CompletedField>::Image image;
    image.pixels = frame.rgba;
    image.width = frame.width;
    image.height = frame.height;
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4)
      std::swap(image.pixels[offset], image.pixels[offset + 2]);
    output.image_valid = !image.pixels.empty();
    output.completion.result.presented =
        video_.render(image.pixels, image.width, image.height, surface);
    output.completion.result.available = video_.available();
    output.completion.result.error = video_.error();
    output.completion.result.width = image.width;
    output.completion.result.height = image.height;
    output.completion.completed = PlaybackClock::Clock::now();
    completed_fields_.presented(field, std::move(output), std::move(image));
  });
  auto ready = std::make_shared<std::vector<CompletedField>>();
  session_->observe_frames([this, ready](unsigned field) {
    if (options_.frames && field > options_.frames)
      return;
    CompletedField output;
    output.frame = field;
    ready->push_back(std::move(output));
  });
  advancing_ = true;
  try {
    execution_.repeat([this, ready] {
      auto selected = take_inputs();
      if (options_.test_input && session_->frames >= 120 && session_->frames % 120 < 60)
        selected[0].buttons |= 0x1000;
      session_->input(selected);
      session_->clear_refresh();
      const auto origin = session_->clocks();
      const auto publish = [&] {
        for (auto &output : *ready) {
          const auto field = output.frame;
          output.clocks = session_->clocks();
          for (unsigned port = 0; port < output.feedback.size(); ++port)
            output.feedback[port] = session_->feedback_state(port);
          completed_fields_.metadata(field, std::move(output));
        }
        ready->clear();
      };
      do {
        try {
          session_->run();
        } catch (...) {
          const auto failure = std::current_exception();
          publish();
          std::rethrow_exception(failure);
        }
        publish();
        const auto control = session_->diagnostics().take_control();
        if (control == n64::HostControl::Exit) {
          worker_control_ = control;
          return false;
        }
        if (!session_->refreshed() && session_->clocks() - origin > 10 * n64::clock_frequency)
          throw std::runtime_error("Emulation stopped because no video refresh was produced.");
      } while (!session_->refreshed() && !execution_.interrupted());
      if (session_->refreshed())
        ++refresh_count_;
      if ((options_.frames && session_->frames >= options_.frames) || execution_.interrupted())
        return false;
      return true;
    });
  } catch (...) {
    advancing_ = false;
    session_->observe_frames({});
    session_->connect_present({});
    throw;
  }
}

bool Window::drain_playback() {
  bool consumed_output = false;
  for (unsigned consumed = 0; consumed < 3; ++consumed) {
    auto output = completed_fields_.take();
    if (!output)
      break;
    observed_frames_ = output->metadata.frame;
    feedback_state_ = output->metadata.feedback;
    refresh_presented_image();
    const bool lost_video = video_available_ && !output->presentation.completion.result.available;
    video_available_ = output->presentation.completion.result.available;
    last_frame_ = PlaybackClock::Clock::now();
    refresh(false);
    consumed_output = true;
    if (lost_video && advancing_)
      finish_playback();
  }
  refresh_presented_image();
  const auto refreshed = refresh_count_.load();
  if (refreshed != observed_refreshes_) {
    observed_refreshes_ = refreshed;
    last_frame_ = PlaybackClock::Clock::now();
  }
  input_.rumble(feedback_targets(settings_.mappings, feedback_.update(feedback_state_), host_,
                                 settings_.feedback && !stopped()));
  return consumed_output;
}

void Window::finish_playback() {
  if (!advancing_)
    return;
  execution_.interrupt();
  std::exception_ptr failure;
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
  while (drain_playback()) {
  }
  video_.show(video_available_ && !pixels_.empty());
  if (failure)
    std::rethrow_exception(failure);
}

bool Window::tick() {
  if (!window_)
    return false;
  if (modal_)
    return true;
  if (gdb_->port() || gdb_connected_ || debugger_attached_) {
    finish_playback();
    return synchronous_tick();
  }
  const auto now = PlaybackClock::Clock::now();
  if (now >= next_audio_probe_) {
    audio_.probe();
    next_audio_probe_ = now + std::chrono::milliseconds(500);
    if (audio_.recovery_pending()) {
      finish_playback();
      if (audio_.recover()) {
        rebase();
        refresh();
      }
    }
  }
  if (stopped() != last_stopped_) {
    finish_playback();
    last_stopped_ = stopped();
    clear_input();
    audio_.clear();
    rebase();
    refresh();
  }
  if (advancing_) {
    if (execution_.ready())
      finish_playback();
    else
      drain_playback();
  }
  if (worker_control_.exchange(n64::HostControl::None) == n64::HostControl::Exit) {
    close();
    return false;
  }
  if (options_.frames && observed_frames_ >= options_.frames) {
    finish_playback();
    if (!options_.capture.empty())
      screenshot(options_.capture);
    terminal_frames_ = true;
    close();
    return false;
  }
  if (!session_ || stopped() || failed_) {
    input_.stop();
    last_frame_ = now;
    return true;
  }
  if (advancing_)
    publish_inputs();
  else
    start_playback();
  return true;
}

} // namespace cupid::desktop::linux_host
