#pragma once

#include "core/system/console.hpp"
#include "renderer/video/settings.hpp"
#include <chrono>
#include <memory>

namespace cupid::desktop {

class VideoOutput {
public:
  explicit VideoOutput(n64::Console &console, bool hardware_rendering = true,
                       n64::RenderSettings settings = {});
  ~VideoOutput();
  VideoOutput(const VideoOutput &) = delete;
  VideoOutput &operator=(const VideoOutput &) = delete;
  void begin_frame(bool field);
  n64::VideoFrame read_frame();
  bool requires_hardware() const;
  bool crashed() const;
  bool hardware_rendering() const;
  static bool supports_hardware();
  void scanout_settings(n64::RenderSettings settings);
  void synchronize();
  void measure(bool enabled);
  std::chrono::nanoseconds host_time() const;
  std::vector<std::uint8_t> capture_state();
  void restore_state(std::span<const std::uint8_t> bytes);

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::desktop
