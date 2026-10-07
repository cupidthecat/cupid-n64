#include "core/cartridge/flash/flash.hpp"

namespace cupid::n64 {

bool FlashRam::select(std::uint32_t address, PeripheralTiming) {
  if (data_.empty() || address < 0x08000000 || address > 0x0fffffff)
    return false;
  offset_ = address - 0x08000000;
  command_valid_ = false;
  burst_ = 0;
  return true;
}

std::optional<std::uint16_t> FlashRam::read_half(PeripheralTiming) {
  if (data_.empty() || open_bus_)
    return {};
  if (mode_ == Mode::Status) {
    if (offset_ & 0x20000) {
      open_bus_ = true;
      return {};
    }
    const std::uint16_t value = stale_ ? stale_value_ : std::uint16_t(status_);
    if (!stale_)
      stale_value_ = value;
    stale_ = false;
    offset_ += 2;
    ++burst_;
    return value;
  }
  if (mode_ == Mode::Silicon) {
    if ((offset_ & 0x20000) && !macronix()) {
      open_bus_ = true;
      return {};
    }
    const std::array<std::uint16_t, 4> id{0x1111, 0x8001, model_.manufacturer, model_.device};
    const auto value = id[burst_ < 4 || macronix() ? burst_ & 3 : 3];
    stale_value_ = value;
    offset_ += 2;
    ++burst_;
    return value;
  }
  if (mode_ == Mode::Page) {
    const auto index = offset_ & 0x7e;
    const auto value = static_cast<std::uint16_t>((unsigned(page_[index]) << 8) | page_[index + 1]);
    stale_value_ = value;
    offset_ += 2;
    return value;
  }
  const auto mask = model_.word_indexed ? 0x3fffu : 0x7fffu;
  const auto offset = (model_.word_indexed ? offset_ << 1 : offset_) & ~1u;
  const auto value =
      offset < data_.size()
          ? static_cast<std::uint16_t>((unsigned(data_[offset]) << 8) | data_[offset + 1])
          : std::uint16_t(0);
  stale_value_ = value;
  offset_ = (offset_ & ~mask) | ((offset_ + (model_.word_indexed ? 1 : 2)) & mask);
  return value;
}

void FlashRam::write_half(std::uint16_t value, PeripheralTiming) {
  if (data_.empty())
    return;
  const auto offset = offset_ & ~1u;
  if (offset == 0x10000 || offset == 0x10002) {
    if (!command_valid_) {
      command_high_ = value;
      command_valid_ = true;
      offset_ += 2;
      return;
    }
    command_valid_ = false;
    offset_ += 2;
    command((std::uint32_t(command_high_) << 16) | value);
    return;
  }
  if (mode_ == Mode::Status) {
    if (!macronix())
      status_ &= ~12;
    else if (!offset && !value)
      status_ |= 12;
  } else if (mode_ == Mode::Page) {
    const auto index = offset & 127;
    if (macronix()) {
      page_[index] = static_cast<std::uint8_t>(value >> 8);
      page_[index + 1] = static_cast<std::uint8_t>(value);
    } else {
      page_[index] &= static_cast<std::uint8_t>(value >> 8);
      page_[index + 1] &= static_cast<std::uint8_t>(value);
    }
  }
  offset_ += 2;
}

} // namespace cupid::n64
