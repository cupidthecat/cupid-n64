#include "core/devices/mi/mips_interface.hpp"
#include <algorithm>
#include <bit>
#include <utility>

namespace cupid::n64 {

MipsInterface::MipsInterface(Rdram &ram) : ram_(ram) {
  power();
}

void MipsInterface::connect(std::function<void(bool)> interrupt, std::function<void()> freeze) {
  interrupt_ = std::move(interrupt);
  freeze_ = std::move(freeze);
  poll();
}

void MipsInterface::power() {
  lines_ = 63;
  masks_ = repeat_length_ = 0;
  repeat_ = ebus_ = register_select_ = frozen_ = false;
}

void MipsInterface::poll() {
  if (interrupt_)
    interrupt_((lines_ & masks_) != 0);
}

void MipsInterface::raise(Interrupt source) {
  lines_ |= 1u << static_cast<unsigned>(source);
  poll();
}

void MipsInterface::lower(Interrupt source) {
  lines_ &= ~(1u << static_cast<unsigned>(source));
  poll();
}

std::uint32_t MipsInterface::read_word(std::uint32_t address) const {
  switch ((address & 15) >> 2) {
  case 0:
    return repeat_length_ | (unsigned(repeat_) << 7) | (unsigned(ebus_) << 8) |
           (unsigned(register_select_) << 9);
  case 1:
    return 0x02020102;
  case 2:
    return lines_;
  default:
    return masks_;
  }
}

void MipsInterface::write_word(std::uint32_t address, std::uint32_t value) {
  const auto index = (address & 15) >> 2;
  if (index == 0) {
    repeat_length_ = value & 127;
    if (value & 0x80)
      repeat_ = false;
    if (value & 0x100)
      repeat_ = true;
    if (value & 0x200)
      ebus_ = false;
    if (value & 0x400)
      ebus_ = true;
    if (value & 0x800)
      lower(Interrupt::Display);
    if (value & 0x1000)
      register_select_ = false;
    if (value & 0x2000)
      register_select_ = true;
  }
  if (index == 3) {
    for (unsigned bit = 0; bit < 6; ++bit) {
      if (value & (1u << (bit * 2)))
        masks_ &= ~(1u << bit);
      if (value & (2u << (bit * 2)))
        masks_ |= 1u << bit;
    }
    poll();
  }
}

void MipsInterface::freeze() {
  frozen_ = true;
  if (freeze_)
    freeze_();
}

BusRead MipsInterface::read_rdram(std::uint32_t address, unsigned bytes) {
  if (address <= 0x03efffff)
    return {ram_.read(address, bytes, ebus_)};
  const auto value = ram_.read_word(address);
  const auto second = bytes == 8 ? ram_.read_word(address + 4) : 0;
  std::uint64_t result = value;
  if (bytes == 8)
    result = (std::uint64_t(value) << 32) | second;
  else if (bytes != 4)
    result = value >> ((4 - bytes - (address & (4 - bytes))) * 8);
  if (!register_select_ && (address & 4))
    result = 0;
  return {result, 40};
}

void MipsInterface::repeat_write(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  unsigned length = repeat_length_ + 1;
  if (bytes == 1) {
    value &= 0xffffffffu >> (24 - (address & 3) * 8);
    value = static_cast<std::uint32_t>((value << 24) | (value >> 8));
  } else if (bytes == 2) {
    value &= 0xffffffffu >> (16 - (address & 2) * 8);
    value = static_cast<std::uint32_t>((value << 16) | (value >> 16));
  }
  if (bytes != 8)
    value = (value << 32) | static_cast<std::uint32_t>(value);
  const auto end = std::min((address & ~7u) + length, ram_.size());
  if (end <= address)
    return;
  length = end - address;
  const auto transfer = [&](unsigned count) {
    ram_.write(address, count, value >> ((8 - count) * 8));
    value = count == 8 ? value : std::rotl(value, static_cast<int>(count * 8));
    address = (address & ~0x7ffu) | ((address + count) & 0x7ff);
    length -= count;
  };
  if (address & 1)
    transfer(1);
  if ((address & 2) && length >= 2)
    transfer(2);
  if ((address & 4) && length >= 4)
    transfer(4);
  while (length >= 8)
    transfer(8);
  if (length >= 4)
    transfer(4);
  if (length >= 2)
    transfer(2);
  if (length)
    transfer(1);
}

BusWrite MipsInterface::write_rdram(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  const bool repeat = std::exchange(repeat_, false);
  if (address <= 0x03efffff) {
    if (repeat)
      repeat_write(address, bytes, value);
    else
      ram_.write(address, bytes, value, ebus_);
    return {};
  }
  auto word = static_cast<std::uint32_t>(bytes == 8 ? value >> 32 : value);
  if (bytes != 4 && bytes != 8)
    word <<= (4 - bytes - (address & (4 - bytes))) * 8;
  ram_.write_word(address, word, repeat ? repeat_length_ + 1 : 0);
  return {};
}

BusWrite MipsInterface::read_burst(std::uint32_t address, std::span<std::uint32_t> words) {
  if (ebus_)
    freeze();
  else if (address <= 0x03efffff)
    ram_.read_burst(address, words);
  else {
    std::fill(words.begin(), words.end(), 0);
    words.front() = ram_.read_word(address);
  }
  return {};
}

BusWrite MipsInterface::write_burst(std::uint32_t address, std::span<const std::uint32_t> words) {
  if (ebus_)
    freeze();
  else if (address <= 0x03efffff)
    ram_.write_burst(address, words);
  else
    ram_.write_word(address, words.front());
  return {};
}

} // namespace cupid::n64
