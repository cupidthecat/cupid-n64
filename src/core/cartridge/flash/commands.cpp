#include "core/cartridge/flash/flash.hpp"
#include <algorithm>

namespace cupid::n64 {

void FlashRam::command(std::uint32_t value) {
  const auto command = static_cast<std::uint8_t>(value >> 24);
  open_bus_ = false;
  if (busy_ != Busy::None)
    return;
  if (macronix() && command == 0xd2) {
    if (pending_command_ != command) {
      pending_command_ = command;
      pending_count_ = 1;
      return;
    }
    if (++pending_count_ < 2)
      return;
  }
  pending_command_ = pending_count_ = 0;
  switch (command) {
  case 0x3c:
    erase_ = Erase::Chip;
    break;
  case 0x4b:
    erase_ = Erase::Sector;
    sector_ = static_cast<std::uint8_t>((value & 0x3ff) >> 7);
    break;
  case 0x78: {
    if (erase_ == Erase::None)
      return;
    const bool chip = erase_ == Erase::Chip;
    if (chip)
      std::fill(data_.begin(), data_.end(), 255);
    else
      std::fill_n(data_.begin() + (unsigned(sector_) << 14), 16384, 255);
    erase_ = Erase::None;
    start(Busy::Erase, chip ? model_.chip_clocks : model_.sector_clocks);
    break;
  }
  case 0xa5: {
    const auto base = (value & 0x3ff) << 7;
    for (unsigned n = 0; n < page_.size(); ++n)
      data_[base + n] &= page_[n];
    page_.fill(255);
    start(Busy::Program, model_.program_clocks);
    break;
  }
  case 0xb4:
    mode_ = Mode::Page;
    break;
  case 0xd2:
    mode_ = Mode::Status;
    stale_ = macronix();
    break;
  case 0xe1:
    mode_ = Mode::Silicon;
    break;
  case 0xf0:
    mode_ = Mode::Array;
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
