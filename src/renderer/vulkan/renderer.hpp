#pragma once

#include "core/devices/rdram/rdram.hpp"
#include <memory>

namespace cupid::n64 {

struct VideoFrame {
  unsigned width = 0, height = 0;
  std::vector<std::uint8_t> rgba;
};

class HardwareRenderer {
public:
  explicit HardwareRenderer(Rdram &ram);
  ~HardwareRenderer();
  HardwareRenderer(const HardwareRenderer &) = delete;
  HardwareRenderer &operator=(const HardwareRenderer &) = delete;
  void submit(std::span<const std::uint32_t> words);
  void synchronize();
  void write_video(unsigned index, std::uint32_t value);
  VideoFrame frame(bool field);
  bool crashed() const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::n64
