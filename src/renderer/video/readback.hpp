#pragma once

#include "renderer/video/frame.hpp"
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace cupid::n64 {

class FrameReadback {
public:
  explicit FrameReadback(std::function<VideoFrame()> read);
  ~FrameReadback();
  FrameReadback(const FrameReadback &) = delete;
  FrameReadback &operator=(const FrameReadback &) = delete;
  void start();
  void wait();
  VideoFrame take();

private:
  void run();
  std::function<VideoFrame()> read_;
  std::mutex mutex_;
  std::condition_variable condition_;
  VideoFrame frame_;
  std::exception_ptr error_;
  bool pending_ = false, stopping_ = false;
  std::thread worker_;
};

} // namespace cupid::n64
