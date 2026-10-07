#include "core/controller/accessories/transfer_pak.hpp"

namespace cupid::n64 {

void TransferPak::connect(std::shared_ptr<TransferCartridge> cartridge) {
  cartridge_ = std::move(cartridge);
  empty_board_ = false;
}

std::uint8_t TransferPak::read(std::uint16_t address) {
  address &= 0x7fff;
  if (!enabled_)
    return 0;
  if (address < 0x2000)
    return 0x84;
  if (address < 0x3000)
    return static_cast<std::uint8_t>(bank_);
  if (address < 0x4000) {
    const auto status = static_cast<std::uint8_t>(
        0x80 | unsigned(cartridge_enabled_) | (reset_ << 2) |
        ((!empty_board_ && (!cartridge_ || !cartridge_->present())) ? 0x40 : 0));
    if (cartridge_enabled_ && reset_ == 3)
      reset_ = 2;
    else if (!cartridge_enabled_ && reset_ && reset_ <= 2)
      --reset_;
    return status;
  }
  if (!cartridge_enabled_)
    return 0;
  const auto mapped = static_cast<std::uint16_t>(bank_ * 0x4000 + address - 0x4000);
  if (mapped > 0x7fff && (mapped < 0xa000 || mapped > 0xbfff))
    return 0;
  return cartridge_ ? cartridge_->read(mapped) : 255;
}

void TransferPak::write(std::uint16_t address, std::uint8_t value) {
  address &= 0x7fff;
  if (address < 0x2000) {
    const auto was_enabled = enabled_;
    if (value == 0x84)
      enabled_ = true;
    if (value == 0xfe)
      enabled_ = false;
    if (!was_enabled && enabled_) {
      bank_ = 3;
      cartridge_enabled_ = false;
      reset_ = 0;
    }
    return;
  }
  if (!enabled_)
    return;
  if (address < 0x3000) {
    bank_ = value <= 3 ? value : 0;
    return;
  }
  if (address < 0x4000) {
    const auto was_enabled = cartridge_enabled_;
    cartridge_enabled_ = value & 1;
    if (!was_enabled && cartridge_enabled_) {
      reset_ = 3;
      if (cartridge_)
        cartridge_->power();
      else
        empty_board_ = true;
    }
    return;
  }
  if (!cartridge_enabled_ || !cartridge_)
    return;
  const auto mapped = static_cast<std::uint16_t>(bank_ * 0x4000 + address - 0x4000);
  if (mapped <= 0x7fff || (mapped >= 0xa000 && mapped <= 0xbfff))
    cartridge_->write(mapped, value);
}

} // namespace cupid::n64
