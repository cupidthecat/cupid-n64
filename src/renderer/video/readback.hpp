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
  void start(unsigned field = 0);
  void wait();
  void wait_presented();
  // Stop frame submission before replacing the presenter.
  void connect_present(std::function<void(unsigned, const VideoFrame &)> present);
  VideoFrame take();
  VideoFrame capture();
  void restore(VideoFrame frame);

private:
  void run();
  std::function<VideoFrame()> read_;
  std::function<void(unsigned, const VideoFrame &)> present_;
  std::mutex mutex_;
  std::condition_variable condition_;
  VideoFrame frame_;
  std::exception_ptr error_;
  unsigned field_ = 0;
  bool pending_ = false, presenting_ = false, stopping_ = false;
  std::thread worker_;
};

} // namespace cupid::n64
