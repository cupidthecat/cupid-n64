#pragma once

#include "core/devices/joybus/device.hpp"

namespace cupid::n64 {

class Mouse : public JoybusDevice {
public:
  void input(bool left, bool right, std::int32_t x, std::int32_t y);
  std::uint32_t state() const;
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  std::int8_t x_ = 0;
  std::int8_t y_ = 0;
  bool left_ = false;
  bool right_ = false;
};

} // namespace cupid::n64
