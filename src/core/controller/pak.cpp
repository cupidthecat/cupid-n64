#include "core/controller/gamepad.hpp"
#include <array>

namespace cupid::n64 {

void Gamepad::format() {
  const auto field_a = random_() & 63;
  const auto field_b = random_() & 0x7ffff;
  const auto field_c = random_() & 0x7ffffff;
  const auto banks = static_cast<unsigned>(ram_.size() / 0x8000);
  const auto half = [&](unsigned address, std::uint16_t value) {
    ram_[address] = static_cast<std::uint8_t>(value >> 8);
    ram_[address + 1] = static_cast<std::uint8_t>(value);
  };
  for (unsigned area : {1u, 3u, 4u, 6u}) {
    const auto base = area * 32;
    ram_[base + 1] = static_cast<std::uint8_t>(field_a);
    for (unsigned n = 0; n < 4; ++n) {
      ram_[base + 4 + n] = static_cast<std::uint8_t>(field_b >> ((3 - n) * 8));
      ram_[base + 8 + n] = static_cast<std::uint8_t>(field_c >> ((3 - n) * 8));
    }
    half(base + 24, 1);
    ram_[base + 26] = static_cast<std::uint8_t>(banks);
    std::uint16_t sum = 0;
    std::uint16_t inverted = 0;
    for (unsigned n = 0; n < 28; n += 2) {
      const auto value =
          static_cast<std::uint16_t>((unsigned(ram_[base + n]) << 8) | ram_[base + n + 1]);
      sum = static_cast<std::uint16_t>(sum + value);
      inverted = static_cast<std::uint16_t>(inverted + static_cast<std::uint16_t>(~value));
    }
    half(base + 28, sum);
    half(base + 30, inverted);
  }
  for (unsigned bank = 0; bank < banks; ++bank) {
    const auto first_data = bank == 0 ? 3 + banks * 2 : 1;
    for (const auto page : {1 + bank, 1 + banks + bank}) {
      for (unsigned slot = first_data; slot < 128; ++slot)
        ram_[page * 256 + slot * 2 + 1] = 3;
      unsigned checksum = 0;
      for (unsigned n = 2; n < 256; ++n)
        checksum += ram_[(1 + bank) * 256 + n];
      ram_[page * 256 + 1] = static_cast<std::uint8_t>(checksum);
    }
  }
}

} // namespace cupid::n64
