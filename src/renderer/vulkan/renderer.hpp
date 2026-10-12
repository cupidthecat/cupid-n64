#pragma once

#include "core/devices/rdram/rdram.hpp"
#include "renderer/video/frame.hpp"
#include "renderer/video/settings.hpp"
#include <memory>

namespace cupid::n64 {

class HardwareRenderer {
public:
  explicit HardwareRenderer(Rdram &ram, RenderSettings settings = {});
  ~HardwareRenderer();
  HardwareRenderer(const HardwareRenderer &) = delete;
  HardwareRenderer &operator=(const HardwareRenderer &) = delete;
  void submit(std::span<const std::uint32_t> words);
  void synchronize();
  void write_video(unsigned index, std::uint32_t value);
  void begin_frame(bool field);
  VideoFrame read_frame();
  VideoFrame frame(bool field);
  bool crashed() const;
  RenderSettings settings() const;
  void scanout_settings(RenderSettings settings);

private:
  friend class RendererState;
  void bind_thread() const;
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::n64
