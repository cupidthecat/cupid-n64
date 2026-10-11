#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace test::cpu_bus {
constexpr unsigned WriteCode = 0x7000, ReadCode = 0x7040, Stop = 0x7800;
constexpr unsigned RamBytes = 0x4000;
constexpr std::array<std::uint32_t, 3> WriteWords{0xac220000, 0x03e00008, 0};
constexpr std::array<std::uint32_t, 3> ReadWords{0x8c220000, 0x03e00008, 0};
constexpr std::uint8_t pattern(unsigned address, unsigned seed) {
  return static_cast<std::uint8_t>(address * 47 + (address >> 7) * 13 + seed);
}
template <class P> void initialize_memory(P &p) {
  p.initial_write(0x04700008, 0);
  p.initial_write(0x0470000c, 0x14);
  p.initial_write(0x04300000, 0x10f);
  p.initial_write(0x03f80008, 0x00080008);
  for (unsigned chip = 0; chip < 4; ++chip) {
    p.initial_write(0x03f0000c, 0x02000000);
    p.initial_write(0x03f00004, (chip + 4) * 2 << 26);
  }
  for (unsigned chip = 0; chip < 4; ++chip)
    p.initial_write(0x03f00004 + (chip + 4) * 0x800, chip * 2 << 26);
  p.initial_write(0x04500010, 1103);
  p.install();
}
} // namespace test::cpu_bus
