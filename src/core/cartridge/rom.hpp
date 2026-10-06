#pragma once

#include "core/devices/pi/device.hpp"
#include <vector>

namespace cupid::n64 {

class CartridgeRom : public PeripheralMemory {
public:
  bool load(std::span<const std::uint8_t> bytes);
  bool select(std::uint32_t address, PeripheralTiming timing) override;
  std::span<const std::uint8_t> data() const {
    return data_;
  }

private:
  std::vector<std::uint8_t> data_;
};

} // namespace cupid::n64
