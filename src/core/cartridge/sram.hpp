#pragma once

#include "core/devices/pi/device.hpp"
#include <vector>

namespace cupid::n64 {

class Sram : public PeripheralMemory {
public:
  explicit Sram(unsigned size = 0);
  bool select(std::uint32_t address, PeripheralTiming timing) override;
  std::span<std::uint8_t> data() {
    return data_;
  }

private:
  friend class CoreState;
  std::vector<std::uint8_t> data_;
};

} // namespace cupid::n64
