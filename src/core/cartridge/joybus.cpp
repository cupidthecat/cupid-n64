#include "core/cartridge/joybus.hpp"

namespace cupid::n64 {

JoybusStatus CartridgeJoybus::communicate(std::span<const std::uint8_t> input,
                                          std::span<std::uint8_t> output) {
  if (!input.empty() && input[0] >= 6 && input[0] <= 8)
    return rtc_.communicate(input, output);
  return eeprom_.communicate(input, output);
}

} // namespace cupid::n64
