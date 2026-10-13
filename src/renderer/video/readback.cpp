#include "renderer/video/readback.hpp"
#include <utility>

namespace cupid::n64 {

FrameReadback::FrameReadback(std::function<VideoFrame()> read)
    : read_(std::move(read)), worker_([this] { run(); }) {}

FrameReadback::~FrameReadback() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  condition_.notify_all();
  worker_.join();
}

void FrameReadback::start(unsigned field) {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_ && !presenting_; });
  if (error_)
    std::rethrow_exception(error_);
  field_ = field;
  pending_ = true;
  condition_.notify_all();
}

void FrameReadback::wait() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_; });
  if (error_)
    std::rethrow_exception(error_);
}

void FrameReadback::wait_presented() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_ && !presenting_; });
  if (error_)
    std::rethrow_exception(error_);
}

void FrameReadback::connect_present(std::function<void(unsigned, const VideoFrame &)> present) {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_ && !presenting_; });
  present_ = std::move(present);
}

VideoFrame FrameReadback::take() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_; });
  if (error_)
    std::rethrow_exception(error_);
  return std::exchange(frame_, {});
}

VideoFrame FrameReadback::capture() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_; });
  if (error_)
    std::rethrow_exception(error_);
  return frame_;
}

void FrameReadback::restore(VideoFrame frame) {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_ && !presenting_; });
  frame_ = std::move(frame);
  error_ = {};
}

void FrameReadback::run() {
  std::unique_lock lock(mutex_);
  for (;;) {
    condition_.wait(lock, [this] { return pending_ || stopping_; });
    if (!pending_)
      return;
    const auto field = field_;
    const bool present = bool(present_);
    lock.unlock();
    VideoFrame frame, stored;
    std::exception_ptr error;
    try {
      frame = read_();
      stored = present ? frame : std::move(frame);
    } catch (...) {
      error = std::current_exception();
    }
    lock.lock();
    frame_ = std::move(stored);
    error_ = error;
    pending_ = false;
    presenting_ = present && !error;
    condition_.notify_all();
    if (presenting_) {
      lock.unlock();
      try {
        present_(field, frame);
      } catch (...) {
        error = std::current_exception();
      }
      lock.lock();
      if (error)
        error_ = error;
      presenting_ = false;
      condition_.notify_all();
    }
  }
}

} // namespace cupid::n64
