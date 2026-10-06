#include "core/cartridge/rom.hpp"

namespace cupid::n64 {

bool CartridgeRom::load(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 4096 || bytes.size() > 0x0fc00000 || (bytes.size() & 3))
    return false;
  unsigned swap = 0;
  const auto magic = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
                     (std::uint32_t(bytes[2]) << 8) | bytes[3];
  switch (magic) {
  case 0x80371240:
    break;
  case 0x37804012:
    swap = 1;
    break;
  case 0x40123780:
    swap = 3;
    break;
  default:
    return false;
  }
  data_.resize(bytes.size());
  for (std::size_t n = 0; n < bytes.size(); ++n)
    data_[n] = bytes[n ^ swap];
  view_ = {};
  offset_ = 0;
  return true;
}

bool CartridgeRom::select(std::uint32_t address, PeripheralTiming timing) {
  if (!timing.permits(minimum_) || address < 0x10000000 || address - 0x10000000 >= data_.size())
    return false;
  view_ = data_;
  offset_ = address - 0x10000000;
  writable_ = false;
  return true;
}

} // namespace cupid::n64
