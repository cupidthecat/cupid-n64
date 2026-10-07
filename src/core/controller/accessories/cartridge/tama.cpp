#include "implementation.hpp"

namespace cupid::n64 {
namespace {
unsigned bcd(unsigned value) {
  return (value / 10) * 16 + value % 10;
}
unsigned decimal(unsigned value) {
  return (value >> 4) * 10 + (value & 15);
}
} // namespace

std::uint8_t HandheldCartridge::Implementation::tama_read(std::uint16_t address) {
  if (address & 1)
    return 255;
  if (select == 10)
    return static_cast<std::uint8_t>(0xf0 | unsigned(ready));
  if (mode <= 1) {
    if (select == 12)
      return static_cast<std::uint8_t>(0xf0 | (output & 15));
    if (select == 13)
      return static_cast<std::uint8_t>(0xf0 | (output >> 4));
  }
  if ((mode == 2 || mode == 4) && (select == 12 || select == 13)) {
    const std::array<unsigned, 8> digits{unsigned(calendar[4] % 10), unsigned(calendar[4] / 10),
                                         unsigned(calendar[3] % 10), unsigned(calendar[3] / 10),
                                         unsigned(calendar[2] / 10), unsigned(calendar[2] % 10),
                                         unsigned(calendar[1] / 10), unsigned(calendar[1] % 10)};
    const auto digit = time_index < digits.size() ? digits[time_index] & 15 : 0;
    time_index = (time_index + 1) & 255;
    return static_cast<std::uint8_t>(0xf0 | digit);
  }
  return 255;
}

void HandheldCartridge::Implementation::tama_write(std::uint16_t address, std::uint8_t value) {
  if (address < 0xa000)
    return;
  if (address & 1) {
    select = value & 15;
    if (select == 10)
      ready = true;
    return;
  }
  switch (select) {
  case 0:
    rom_bank = (rom_bank & 16) | (value & 15);
    break;
  case 1:
    rom_bank = (rom_bank & 15) | ((value & 1) << 4);
    break;
  case 4:
    input = (input & 0xf0) | (value & 15);
    break;
  case 5:
    input = (input & 15) | ((value & 15) << 4);
    break;
  case 6:
    index = (index & 15) | ((value & 1) << 4);
    mode = (value >> 1) & 7;
    break;
  case 7: {
    index = (index & 16) | (value & 15);
    if (!mode)
      store(ram, index, static_cast<std::uint8_t>(input));
    if (mode == 1 && !ram.empty())
      output = memory(ram, index);
    if (mode == 2 && index == 4)
      calendar[4] = static_cast<std::uint8_t>(decimal(input));
    if (mode == 2 && index == 5) {
      calendar[3] = static_cast<std::uint8_t>(decimal(input));
      calendar[6] = static_cast<std::uint8_t>((calendar[6] & 0xfe) | unsigned(calendar[3] >= 12));
    }
    if (mode == 2 && index == 6)
      time_index = 0;
    if (mode == 4 && !index && (input & 15) >= 7 && (input & 15) <= 12) {
      const auto command = (input & 15) - 7;
      const auto field = 2 - command / 2;
      const auto shift = (command & 1) * 4;
      const auto packed = (bcd(calendar[field]) & ~(15u << shift)) | ((input >> 4) << shift);
      calendar[field] = static_cast<std::uint8_t>(decimal(packed));
    }
    if (mode == 4 && index == 2) {
      if ((input & 15) == 10) {
        calendar[6] = static_cast<std::uint8_t>((calendar[6] & ~8u) | ((input & 16) >> 1));
        calendar[5] = 0;
      }
      if ((input & 15) == 11)
        calendar[6] = static_cast<std::uint8_t>((calendar[6] & ~6u) | ((value >> 3) & 6));
      if ((input & 15) == 14)
        calendar[6] = static_cast<std::uint8_t>((calendar[6] & 15) | (input & 0xf0));
    }
    break;
  }
  }
}

} // namespace cupid::n64
