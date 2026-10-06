#include "core/devices/rdram/rdram.hpp"
#include <algorithm>

namespace cupid::n64 {

Rdram::Rdram(RamInterface &interface, RandomGenerator &random, bool expansion)
    : interface_(interface), random_(random), data_((expansion ? 8u : 4u) * 1024 * 1024 / 4),
      hidden_(data_.size() * 2) {
  power();
}

void Rdram::power(bool reset) {
  if (reset)
    return;
  std::fill(data_.begin(), data_.end(), 0);
  std::fill(hidden_.begin(), hidden_.end(), std::uint8_t(0));
  chips_ = {};
  identity_ = false;
  for (unsigned n = 0; n < data_.size() / (2 * 1024 * 1024 / 4); ++n) {
    auto &chip = chips_[n];
    chip.present = true;
    chip.registers[0] = 0xb4190010;
    chip.registers[9] = 0x500;
    chip.registers[2] = 0x23;
    chip.write_delay = 4;
    chip.low_current = 8 + (random_() & 3);
    chip.high_current = 14 + (random_() & 3);
    if (chip.high_current <= chip.low_current)
      chip.high_current = chip.low_current + 1;
  }
}

std::uint16_t Rdram::decode_id(std::uint32_t value) {
  return static_cast<std::uint16_t>(((value >> 26) & 63) | (((value >> 23) & 1) << 6) |
                                    (((value >> 8) & 255) << 7) | (((value >> 7) & 1) << 15));
}

unsigned Rdram::decode_current(std::uint32_t value) {
  return (((value >> 6) & 1) | (((value >> 14) & 1) << 1) | (((value >> 22) & 1) << 2) |
          (((value >> 7) & 1) << 3) | (((value >> 15) & 1) << 4) | (((value >> 23) & 1) << 5)) ^
         63;
}

std::uint32_t Rdram::encode_current(unsigned current) {
  current ^= 63;
  return ((current & 1) << 6) | (((current >> 1) & 1) << 14) | (((current >> 2) & 1) << 22) |
         (((current >> 3) & 1) << 7) | (((current >> 4) & 1) << 15) | (((current >> 5) & 1) << 23);
}

void Rdram::update_mapping() {
  identity_ = true;
  for (unsigned n = 0; n < chips_.size(); ++n) {
    const auto &chip = chips_[n];
    if (chip.present &&
        (!chip.enabled || chip.device_id != n * 2 || chip.current < chip.high_current)) {
      identity_ = false;
      return;
    }
  }
}

std::optional<std::uint32_t> Rdram::translate(std::uint32_t address) {
  if (identity_)
    return address < size() ? std::optional(address) : std::nullopt;
  if (interface_.active()) {
    for (unsigned n = 0; n < chips_.size(); ++n) {
      const auto &chip = chips_[n];
      if (chip.present && chip.enabled && (unsigned(chip.device_id) >> 1) == (address >> 21))
        return n * 0x200000 + (address & 0x1fffff);
    }
  }
  interface_.acknowledge_error();
  return {};
}

Rdram::Chip *Rdram::select_chip(unsigned id) {
  if (!interface_.active())
    return nullptr;
  bool seen_disabled = false;
  for (auto &chip : chips_) {
    if (!chip.present)
      continue;
    const bool match = (chip.device_id & ~1u) == (id & ~1u);
    if (chip.enabled) {
      if (match)
        return &chip;
    } else if (!seen_disabled) {
      seen_disabled = true;
      if (match)
        return &chip;
    }
  }
  return nullptr;
}

std::uint32_t Rdram::read_register(const Chip &chip, unsigned index) const {
  if (!chip.enabled || index >= chip.registers.size())
    return 0;
  if (index == 2)
    return chip.registers[2] | 0x03030203;
  if (index == 3) {
    const auto current =
        chip.auto_current ? chip.internal_current : decode_current(chip.registers[3]);
    return ((chip.registers[3] & ~0x00c0c0c0u) | encode_current(current)) ^ 0xc0c0c0c0;
  }
  return chip.registers[index];
}

void Rdram::write_register(Chip &chip, unsigned index, std::uint32_t value,
                           unsigned repeat_length) {
  if (index >= chip.registers.size())
    return;
  if (chip.write_delay != 1) {
    if (repeat_length < 16)
      return;
    value = (value << 16) | (value >> 16);
  }
  if (index == 0 || index == 9)
    return;
  if (index == 1)
    chip.device_id = decode_id(value);
  if (index == 2) {
    value &= 0x38381838;
    chip.write_delay = (value >> 3) & 7;
  }
  if (index == 3) {
    chip.enabled = value & 0x02000000;
    chip.auto_current = value & 0x80000000;
    chip.current = decode_current(value);
    if (chip.auto_current)
      chip.internal_current = chip.current;
  }
  chip.registers[index] = value;
  update_mapping();
}

std::uint32_t Rdram::read_word(std::uint32_t address) {
  if ((address & 0x80000) || !interface_.active())
    return 0;
  auto *chip = select_chip((address >> 10) & 0x1ff);
  if (!chip)
    return 0;
  if (address & 0x200)
    return chip->row;
  return read_register(*chip, (address >> 2) & 15);
}

void Rdram::write_word(std::uint32_t address, std::uint32_t value, unsigned repeat_length) {
  if (!interface_.active())
    return;
  const auto apply = [&](Chip &chip) {
    if (address & 0x200)
      chip.row = value;
    else
      write_register(chip, (address >> 2) & 15, value, repeat_length);
  };
  if (address & 0x80000) {
    for (auto &chip : chips_) {
      if (chip.present)
        apply(chip);
    }
  } else if (auto *chip = select_chip((address >> 10) & 0x1ff))
    apply(*chip);
}

std::uint64_t Rdram::degrade(std::uint64_t value, const Chip &chip) {
  if (chip.current >= chip.high_current)
    return value;
  if (chip.current <= chip.low_current)
    return 0;
  const auto progress =
      (chip.current - chip.low_current) * 256 / (chip.high_current - chip.low_current);
  std::uint64_t result = 0;
  for (unsigned bit = 0; bit < 64; ++bit) {
    if ((value & (1ull << bit)) && (random_() & 255) < progress)
      result |= 1ull << bit;
  }
  return result;
}

std::uint64_t Rdram::read_raw(std::uint32_t address, unsigned bytes) const {
  address &= ~(bytes - 1u);
  const auto word = data_[address >> 2];
  if (bytes == 8)
    return (std::uint64_t(word) << 32) | data_[(address >> 2) + 1];
  if (bytes == 4)
    return word;
  return (word >> ((4 - bytes - (address & 3)) * 8)) & ((1u << (bytes * 8)) - 1);
}

void Rdram::write_raw(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  address &= ~(bytes - 1u);
  auto &word = data_[address >> 2];
  if (bytes == 8) {
    word = static_cast<std::uint32_t>(value >> 32);
    data_[(address >> 2) + 1] = static_cast<std::uint32_t>(value);
  } else if (bytes == 4)
    word = static_cast<std::uint32_t>(value);
  else {
    const auto shift = (4 - bytes - (address & 3)) * 8;
    const auto mask = ((1u << (bytes * 8)) - 1) << shift;
    word = (word & ~mask) | ((static_cast<std::uint32_t>(value) << shift) & mask);
  }
}

std::uint32_t Rdram::hidden_nibble(std::uint32_t address) const {
  return ((hidden_[address >> 1] & 3) << 2) | (hidden_[(address >> 1) + 1] & 3);
}

void Rdram::write_hidden_bit(std::uint32_t address, bool value) {
  const auto shift = 1 - (address & 1);
  auto &bits = hidden_[address >> 1];
  bits = static_cast<std::uint8_t>((bits & ~(1u << shift)) | (unsigned(value) << shift));
}

void Rdram::update_hidden(std::uint32_t address, unsigned bytes, std::uint64_t value, bool ebus) {
  address &= ~(bytes - 1u);
  if (ebus) {
    for (unsigned n = 0; n < bytes; ++n) {
      bool bit;
      if (bytes == 1)
        bit = (address & 3) == 3 && (value & 1);
      else if (bytes == 2)
        bit = (address & 2) && ((value >> (1 - n)) & 1);
      else
        bit = (value >> ((bytes - 4 - (n & ~3u)) * 8 + 3 - (n & 3))) & 1;
      write_hidden_bit(address + n, bit);
    }
  } else if (bytes == 1)
    write_hidden_bit(address, (address & 1) && (value & 1));
  else {
    for (unsigned n = 0; n < bytes; n += 2)
      hidden_[(address + n) >> 1] = ((value >> ((bytes - 2 - n) * 8)) & 1) * 3;
  }
}

std::uint64_t Rdram::read(std::uint32_t address, unsigned bytes, bool ebus) {
  const auto mapped = translate(address);
  if (!mapped)
    return 0;
  if (ebus) {
    const auto word = hidden_nibble(*mapped & ~3u);
    if (bytes == 8)
      return (std::uint64_t(word) << 32) | hidden_nibble((*mapped & ~7u) + 4);
    if (bytes == 4)
      return word;
    return (word >> ((4 - bytes - (*mapped & (4 - bytes))) * 8)) & ((1u << (bytes * 8)) - 1);
  }
  const auto value = read_raw(*mapped, bytes);
  return identity_ ? value : degrade(value, chips_[*mapped / 0x200000]);
}

void Rdram::write(std::uint32_t address, unsigned bytes, std::uint64_t value, bool ebus) {
  if (const auto mapped = translate(address)) {
    write_raw(*mapped, bytes, value);
    update_hidden(*mapped, bytes, value, ebus);
  }
}

void Rdram::read_burst(std::uint32_t address, std::span<std::uint32_t> words) {
  const auto mapped = translate(address);
  if (!mapped) {
    std::fill(words.begin(), words.end(), 0);
    return;
  }
  for (unsigned n = 0; n < words.size(); ++n) {
    const auto value = read_raw(*mapped + n * 4, 4);
    words[n] =
        static_cast<std::uint32_t>(identity_ ? value : degrade(value, chips_[*mapped / 0x200000]));
  }
}

void Rdram::write_burst(std::uint32_t address, std::span<const std::uint32_t> words) {
  if (const auto mapped = translate(address)) {
    for (unsigned n = 0; n < words.size(); ++n) {
      write_raw(*mapped + n * 4, 4, words[n]);
      update_hidden(*mapped + n * 4, 4, words[n], false);
    }
  }
}

} // namespace cupid::n64
