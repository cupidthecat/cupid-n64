#include "core/devices/ri/ram_interface.hpp"

namespace cupid::n64 {

void RamInterface::power(bool reset) {
  if (!reset) {
    registers_ = {};
    current_loaded_ = false;
  }
}

std::uint32_t RamInterface::read_word(std::uint32_t address) const {
  const auto index = (address & 0x1f) >> 2;
  if (index == 2)
    return (registers_[6] & 1) | 6 | (registers_[0] & 8) | (registers_[3] & 16);
  return registers_[index];
}

void RamInterface::write_word(std::uint32_t address, std::uint32_t value) {
  const auto index = (address & 0x1f) >> 2;
  if (index == 2)
    current_loaded_ = true;
  if (index == 6)
    value = 0;
  if (index == 7)
    value = 0xff;
  registers_[index] = value;
}

} // namespace cupid::n64
