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

void FrameReadback::start() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_; });
  if (error_)
    std::rethrow_exception(error_);
  pending_ = true;
  condition_.notify_all();
}

void FrameReadback::wait() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_; });
  if (error_)
    std::rethrow_exception(error_);
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
  condition_.wait(lock, [this] { return !pending_; });
  frame_ = std::move(frame);
  error_ = {};
}

void FrameReadback::run() {
  std::unique_lock lock(mutex_);
  for (;;) {
    condition_.wait(lock, [this] { return pending_ || stopping_; });
    if (!pending_)
      return;
    lock.unlock();
    VideoFrame frame;
    std::exception_ptr error;
    try {
      frame = read_();
    } catch (...) {
      error = std::current_exception();
    }
    lock.lock();
    frame_ = std::move(frame);
    error_ = error;
    pending_ = false;
    condition_.notify_all();
  }
}

} // namespace cupid::n64
