#include "desktop/windows/window.hpp"
#include <algorithm>
#include <utility>

namespace cupid::desktop {
namespace {
struct PresentationGuard {
  bool &active;
  bool previous;
  explicit PresentationGuard(bool &value) : active(value), previous(std::exchange(value, true)) {}
  ~PresentationGuard() {
    active = previous;
  }
};
} // namespace

bool Window::image_valid() const {
  return pixel_width_ && pixel_height_ && pixel_width_ <= n64::maximum_frame_width &&
         pixel_height_ <= n64::maximum_frame_height &&
         pixels_.size() == std::size_t(pixel_width_) * pixel_height_ * 4;
}

VideoSurface Window::video_surface() {
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
  return {cached_viewport_width_, cached_viewport_height_, display_scale_, display_aspect_,
          !fast_forward_ && playback_sync_ == PlaybackSync::Video};
}

PresentationWorker::Submission Window::video_submission(unsigned field) {
  PresentationWorker::Submission submission;
  submission.tag.field = field;
  if (!image_valid()) {
    video_presenter_.hide();
    return submission;
  }
  submission.pixels = pixels_;
  submission.width = pixel_width_;
  submission.height = pixel_height_;
  submission.surface = video_surface();
  const PresentationGuard guard(presenting_field_);
  video_presenter_.prepare(submission.surface);
  video_presenter_.show(video_available_);
  return submission;
}

void Window::refresh_presented_image() {
  auto image = completed_fields_.take_image();
  if (!image)
    return;
  const bool available = !pixels_.empty();
  pixels_ = std::move(image->pixels);
  pixel_width_ = image->width;
  pixel_height_ = image->height;
  if (available != !pixels_.empty())
    update_display();
  InvalidateRect(window_, nullptr, FALSE);
}

bool Window::present_video() {
  if (presentation_.pending())
    return field_presented_;
  presentation_.submit(video_submission(last_frame_), [this](const auto &completion) {
    const PresentationGuard guard(presenting_field_);
    field_presented_ = completion.result.presented;
    video_available_ = completion.result.available;
    video_presenter_.show(field_presented_);
  });
  presentation_.quiesce();
  return field_presented_;
}

void Window::observe_presentation(FieldTrace::Observation &observation,
                                  const PresentationWorker::Completion &completion) {
  const PresentationGuard guard(presenting_field_);
  field_presented_ = completion.result.presented;
  video_available_ = completion.result.available;
  video_presenter_.show(field_presented_);
  observation.video_presented = field_presented_;
  observation.gpu_ready = video_available_;
  observation.presentation_ns = static_cast<std::uint64_t>(completion.duration.count());
  observation.presentation_field = completion.tag.field;
  observation.presentation_generation = completion.tag.generation;
  observation.presentation_sequence = completion.tag.sequence;
  if (field_trace_)
    field_trace_->presented(observation, completion.completed);
  UpdateWindow(window_);
}

void Window::present_field(FieldTrace::Observation &observation) {
  observation.image_valid = image_valid();
  presentation_.submit(video_submission(observation.field),
                       [this, &observation](const auto &completion) {
                         observe_presentation(observation, completion);
                       });
  presentation_.quiesce();
}

} // namespace cupid::desktop
