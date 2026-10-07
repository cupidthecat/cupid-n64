#include "core/cartridge/isviewer/isviewer.hpp"
#include "core/devices/pi/peripheral_interface.hpp"
#include <algorithm>

namespace cupid::n64 {

void IsViewer::connect(std::size_t cartridge_size) {
  ram_.resize(cartridge_size && cartridge_size <= 0x03ff0000 ? 0x10000 : 0);
  power();
}

void IsViewer::power() {
  std::fill(ram_.begin(), ram_.end(), 0);
  offset_ = 0;
}

bool IsViewer::select(std::uint32_t address, PeripheralTiming) {
  if (ram_.empty() || address < 0x13ff0000 || address > 0x13ffffff)
    return false;
  offset_ = address & 0xfffe;
  return true;
}

std::optional<std::uint16_t> IsViewer::read_half(PeripheralTiming) {
  if (ram_.empty())
    return {};
  const auto offset = offset_ & 0xffff;
  const auto value = static_cast<std::uint16_t>((unsigned(ram_[offset]) << 8) | ram_[offset + 1]);
  offset_ += 2;
  return value;
}

void IsViewer::write_half(std::uint16_t value, PeripheralTiming) {
  if (ram_.empty())
    return;
  pi_.force_finish_write();
  const auto offset = offset_ & 0xffff;
  if (offset == 0x16) {
    // Completing output clears both buffer pointers without storing the count.
    std::fill_n(ram_.begin() + 4, 4, 0);
  } else {
    ram_[offset] = static_cast<std::uint8_t>(value >> 8);
    ram_[offset + 1] = static_cast<std::uint8_t>(value);
  }
  offset_ += 2;
}

} // namespace cupid::n64
