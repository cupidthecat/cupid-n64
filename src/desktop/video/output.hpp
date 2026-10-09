#pragma once

#include "core/system/console.hpp"
#include <memory>

namespace cupid::desktop {

class VideoOutput {
public:
  explicit VideoOutput(n64::Console &console, bool hardware_rendering = true);
  ~VideoOutput();
  VideoOutput(const VideoOutput &) = delete;
  VideoOutput &operator=(const VideoOutput &) = delete;
  void begin_frame(bool field);
  n64::VideoFrame read_frame();
  bool requires_hardware() const;
  bool crashed() const;
  bool hardware_rendering() const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::desktop
