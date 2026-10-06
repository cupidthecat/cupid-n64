#include "core/devices/vi/video_interface.hpp"

namespace cupid::n64 {

std::uint32_t VideoInterface::read_word(std::uint32_t address) const {
  const auto index = (address & 63) >> 2;
  if (index == 4) {
    const auto value = (counter_ << 1) | unsigned(field_);
    if (sync_)
      sync_();
    return value;
  }
  return index < registers_.size() ? registers_[index] : 0;
}

void VideoInterface::write_word(std::uint32_t address, std::uint32_t value) {
  const auto index = (address & 63) >> 2;
  if (write_register_)
    write_register_(index, value);
  constexpr std::uint32_t masks[] = {0xffff,     0xffffff,   0xfff,      0x3ff,      0,
                                     0x3fffffff, 0x3ff,      0x001f0fff, 0x0fff0fff, 0x03ff03ff,
                                     0x03ff03ff, 0x03ff03ff, 0x0fff0fff, 0x0fff0fff};
  if (index == 4)
    interrupts_.lower(Interrupt::Video);
  else if (index < registers_.size())
    registers_[index] = value & masks[index];
}

} // namespace cupid::n64
