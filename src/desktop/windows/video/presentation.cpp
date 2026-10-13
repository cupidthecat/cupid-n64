#include "desktop/windows/window.hpp"
#include <algorithm>
#include <utility>

namespace cupid::desktop {

bool Window::image_valid() const {
  return pixel_width_ && pixel_height_ && pixel_width_ <= n64::maximum_frame_width &&
         pixel_height_ <= n64::maximum_frame_height &&
         pixels_.size() == std::size_t(pixel_width_) * pixel_height_ * 4;
}

bool Window::present_video() {
  if (!image_valid()) {
    video_presenter_.hide();
    return false;
  }
  RECT client{};
  GetClientRect(window_, &client);
  const auto height = std::max(0L, client.bottom - status_height());
  if (client.right > 0 && height > 0) {
    cached_viewport_width_ = static_cast<unsigned>(client.right);
    cached_viewport_height_ = static_cast<unsigned>(height);
  } else if (!cached_viewport_width_ || !cached_viewport_height_) {
    const auto dpi = GetDpiForWindow(window_);
    cached_viewport_width_ = MulDiv(static_cast<int>(viewport_size_.width), dpi ? dpi : 96, 96);
    cached_viewport_height_ = MulDiv(static_cast<int>(viewport_size_.height), dpi ? dpi : 96, 96);
  }
  return video_presenter_.present(pixels_, pixel_width_, pixel_height_, cached_viewport_width_,
                                  cached_viewport_height_, display_scale_, display_aspect_,
                                  !fast_forward_ && playback_sync_ == PlaybackSync::Video);
}

void Window::present_field(FieldTrace::Observation &observation) {
  const auto started =
      field_trace_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  const auto previous = std::exchange(presenting_field_, true);
  try {
    observation.image_valid = image_valid();
    field_presented_ = present_video();
    observation.video_presented = field_presented_;
    observation.gpu_ready = video_presenter_.available();
    if (field_trace_) {
      observation.presentation_ns =
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count());
      field_trace_->presented(observation);
    }
    UpdateWindow(window_);
  } catch (...) {
    presenting_field_ = previous;
    throw;
  }
  presenting_field_ = previous;
}

} // namespace cupid::desktop
