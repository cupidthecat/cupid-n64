#include "core/cartridge/sram.hpp"
#include <bit>

namespace cupid::n64 {

Sram::Sram(unsigned size) {
  if ((size >= 8 && size <= 32768 && std::has_single_bit(size)) ||
      (size > 32768 && size <= 16 * 1024 * 1024 && !(size & 32767)))
    data_.resize(size, 255);
}

bool Sram::select(std::uint32_t address, PeripheralTiming timing) {
  if (data_.empty() || !timing.permits(minimum_) || address < 0x08000000 || address > 0x0fffffff)
    return false;
  const auto offset = address - 0x08000000;
  if (data_.size() > 32768) {
    const auto bank = offset >> 18;
    if (bank >= data_.size() >> 15 || (offset & 0x3ffff) >= 32768)
      return false;
    view_ = std::span(data_).subspan(bank << 15, 32768);
    offset_ = offset & 32767;
  } else {
    view_ = data_;
    offset_ = offset & static_cast<std::uint32_t>(data_.size() - 1);
  }
  writable_ = true;
  return true;
}

} // namespace cupid::n64
