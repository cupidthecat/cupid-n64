#pragma once

#include <cstdint>
#include <span>

namespace cupid::n64 {

struct JoybusStatus {
  bool valid = false;
  bool overflow = false;
};

class JoybusDevice {
public:
  virtual ~JoybusDevice() = default;
  virtual void reset() {}
  virtual JoybusStatus communicate(std::span<const std::uint8_t> input,
                                   std::span<std::uint8_t> output) = 0;
};

unsigned address_crc(std::uint16_t address);
std::uint8_t data_crc(std::span<const std::uint8_t, 32> data);

} // namespace cupid::n64
