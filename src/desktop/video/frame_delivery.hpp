#pragma once

#include "desktop/video/presentation_worker.hpp"
#include "renderer/video/frame.hpp"
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

// Execution and graphics complete independently; UI delivery never holds either worker.
template <typename Metadata> class FrameDelivery {
public:
  struct Image {
    std::vector<std::uint8_t> pixels;
    unsigned width = 0, height = 0;
  };
  struct Presented {
    PresentationWorker::Completion completion;
    std::optional<n64::VideoFrame> capture;
    bool image_valid = false, shader_failed = false;
  };
  struct Output {
    Metadata metadata;
    Presented presentation;
  };

  void metadata(unsigned field, Metadata value) {
    std::lock_guard lock(mutex_);
    metadata_.push_back({{generation_, ++metadata_sequence_, field}, std::move(value)});
  }

  void presented(unsigned field, Presented value, Image image) {
    std::lock_guard lock(mutex_);
    value.completion.tag = {generation_, ++presentation_sequence_, field};
    presented_.push_back(std::move(value));
    image_ = std::move(image);
  }

  std::optional<Output> take() {
    std::lock_guard lock(mutex_);
    if (metadata_.empty() || presented_.empty())
      return {};
    const bool matched = metadata_.front().tag == presented_.front().completion.tag;
    Output result{std::move(metadata_.front().value), std::move(presented_.front())};
    metadata_.pop_front();
    presented_.pop_front();
    if (!matched)
      throw std::logic_error("The displayed image does not match its execution field.");
    return result;
  }

  std::optional<Image> take_image() {
    std::lock_guard lock(mutex_);
    return std::exchange(image_, {});
  }

  // Both producers must be stopped, with accepted outputs delivered.
  void new_generation() {
    std::lock_guard lock(mutex_);
    if (!metadata_.empty() || !presented_.empty())
      throw std::logic_error("Accepted video fields must drain before replacing the session.");
    generation_ = detail::presentation_generation();
    metadata_sequence_ = presentation_sequence_ = 0;
    image_.reset();
  }

private:
  struct Observed {
    PresentationWorker::Tag tag;
    Metadata value;
  };
  std::mutex mutex_;
  std::deque<Observed> metadata_;
  std::deque<Presented> presented_;
  std::optional<Image> image_;
  std::uint64_t generation_ = detail::presentation_generation();
  std::uint64_t metadata_sequence_ = 0, presentation_sequence_ = 0;
};

} // namespace cupid::desktop
