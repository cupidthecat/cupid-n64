#pragma once
#include <array>
#include <cstdint>

namespace test::machine_reset {
constexpr unsigned MemoryBytes = 0x8000;
constexpr std::uint8_t pattern(unsigned address, unsigned seed) {
  return static_cast<std::uint8_t>(address * 37 + (address >> 8) + seed * 61 + 0x29);
}
inline std::array<std::uint8_t, 0x1000> rom() {
  std::array<std::uint8_t, 0x1000> data{};
  for (unsigned n = 0; n < data.size(); ++n)
    data[n] = pattern(n, 7);
  data[0] = 0x80;
  data[1] = 0x37;
  data[2] = 0x12;
  data[3] = 0x40;
  return data;
}
template <class Probe> void initialize(Probe &p) {
  p.write(0x04700008, 0);
  p.write(0x0470000c, 0x14);
  p.write(0x04300000, 0x10f);
  p.write(0x03f80008, 0x00080008);
  const unsigned chips = p.expansion() ? 4 : 2;
  for (unsigned chip = 0; chip < chips; ++chip) {
    p.write(0x03f0000c, 0x02000000);
    p.write(0x03f00004, (chip + 4) * 2 << 26);
  }
  for (unsigned chip = 0; chip < chips; ++chip)
    p.write(0x03f00004 + (chip + 4) * 0x800, chip * 2 << 26);
}
template <class Probe> void observe(Probe &p) {
  p.word(p.pc());
  p.word(p.cpu_clock());
  for (unsigned reg = 0; reg < 32; ++reg)
    p.word(p.gpr(reg));
  for (unsigned reg : {9u, 11u, 12u, 13u, 16u, 30u})
    p.word(p.control(reg));
  p.word(p.identity());
  p.word(p.frozen());
  constexpr std::array<std::array<unsigned, 2>, 8> ranges{{{0x04300000, 4},
                                                           {0x04400000, 14},
                                                           {0x04500000, 6},
                                                           {0x04600000, 13},
                                                           {0x04700000, 8},
                                                           {0x04800000, 7},
                                                           {0x04040000, 8},
                                                           {0x04100000, 8}}};
  for (const auto &range : ranges)
    for (unsigned n = 0; n < range[1]; ++n)
      p.word(p.read(range[0] + n * 4));
  p.word(p.pif_state());
  p.word(p.pif_reset());
  for (unsigned n = 0; n < 64; n += 8)
    p.word(p.pif_memory(n));
  for (unsigned n = 0; n < MemoryBytes; n += 8)
    p.word(p.raw_memory(n));
  for (unsigned n = 0; n < 8192; n += 8)
    p.word(p.rsp_memory(n));
  for (unsigned n = 0; n < 512; n += 8)
    p.word(p.eeprom_memory(n));
  for (unsigned n = 0; n < 512; n += 8)
    p.word(p.sram_memory(n));
  p.word(p.eeprom_status());
  p.internal();
}
template <class Probe> void scenarios(Probe &p) {
  for (bool expanded : {false, true})
    for (bool warm : {false, true})
      for (unsigned seed = 0; seed < 3u; ++seed)
        for (unsigned action = 0; action < 8u; ++action) {
          p.reset(expanded);
          initialize(p);
          p.fill(seed);
          p.dirty_cpu(seed);
          p.write(0x04700010, 0x12345678 ^ seed);
          p.write(0x04400000, 0x1234);
          p.write(0x0430000c, 0xaaa);
          if (action == 0 || action == 7) {
            p.write(0x04600000, 0x4000);
            p.write(0x04600004, 0x10000000);
            p.write(0x0460000c, 0x1ff);
          }
          if (action == 1 || action == 7) {
            p.write(0x04800000, 0x5000);
            p.write(0x04800004, 0x1fc007c0);
          }
          if (action == 2 || action == 7) {
            p.write(0x04500010, 1103);
            p.write(0x04500000, 0x1ff8);
            p.write(0x04500004, 24);
            p.write(0x04500008, 1);
          }
          if (action == 3 || action == 7) {
            p.write(0x04040000, 0x1ff8);
            p.write(0x04040004, 0x1000);
            p.write(0x04040008, 0x1027);
          }
          if (action == 4 || action == 7)
            p.start_eeprom(seed);
          if (action == 5)
            p.request_nmi();
          if (action == 6 || action == 7) {
            p.write(0x0410000c, 8);
            p.write(0x04100000, 0x1000);
            p.write(0x04100004, 0x1020);
          }
          observe(p);
          p.power(warm);
          observe(p);
          p.finish();
        }
}
} // namespace test::machine_reset
