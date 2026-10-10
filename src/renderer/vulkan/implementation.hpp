#pragma once

#include "rdp_device.hpp"
#include "renderer/video/readback.hpp"
#include "renderer/vulkan/renderer.hpp"
#include <atomic>

namespace cupid::n64 {

struct HardwareRenderer::Implementation : RDP::ValidationInterface {
  Rdram &ram;
  Vulkan::Context context;
  Vulkan::Device device;
  std::unique_ptr<RDP::CommandProcessor> processor;
  RDP::VIScanoutBuffer scanout;
  std::atomic<bool> crashed = false;
  std::unique_ptr<FrameReadback> readback;

  explicit Implementation(Rdram &memory);
  ~Implementation();
  VideoFrame read_pixels();
  void report_rdp_crash(RDP::ValidationError, const char *) override;
};

} // namespace cupid::n64
