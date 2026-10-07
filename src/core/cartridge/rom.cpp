#include "core/cartridge/rom.hpp"
#include "core/cartridge/profile/profile.hpp"

namespace cupid::n64 {

bool CartridgeRom::load(std::span<const std::uint8_t> bytes) {
  const auto profile = inspect_cartridge(bytes);
  if (!profile)
    return false;
  data_.resize(bytes.size());
  for (std::size_t n = 0; n < bytes.size(); ++n)
    data_[n] = bytes[n ^ profile->byte_swap];
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
