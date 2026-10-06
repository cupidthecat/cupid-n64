#include "core/devices/joybus/device.hpp"

namespace cupid::n64 {

unsigned address_crc(std::uint16_t address) {
  unsigned result = 0;
  for (unsigned n = 0; n < 16; ++n) {
    const auto polynomial = result & 16 ? 0x15u : 0;
    result = ((result << 1) | ((address >> (15 - n)) & 1)) & 31;
    result ^= polynomial;
  }
  return result;
}

std::uint8_t data_crc(std::span<const std::uint8_t, 32> data) {
  unsigned result = 0;
  for (unsigned n = 0; n < 33; ++n) {
    for (unsigned bit = 8; bit; --bit) {
      const auto polynomial = result & 128 ? 0x85u : 0;
      result = (result << 1) & 255;
      if (n < 32)
        result |= (data[n] >> (bit - 1)) & 1;
      result ^= polynomial;
    }
  }
  return static_cast<std::uint8_t>(result);
}

} // namespace cupid::n64
