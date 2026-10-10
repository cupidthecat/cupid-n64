#pragma once
#include <array>
#include <cstdint>

namespace test::audio_dma {
constexpr unsigned MemoryBytes = 0x20000;
constexpr std::uint8_t pattern(unsigned address) {
  return static_cast<std::uint8_t>(address * 31 + (address >> 9) + 0x83);
}
template <class Probe> void observe(Probe &p) {
  for (unsigned offset = 0; offset < 32; offset += 4)
    p.word(p.read(offset));
  p.word(p.interrupts());
  for (unsigned index = 0; index < 2; ++index) {
    p.word(p.address(index));
    p.word(p.length(index));
  }
  p.word(p.count());
  p.word(p.enabled());
  p.word(p.carry());
  p.word(p.dac_rate());
  p.word(p.bit_rate());
  p.word(p.frequency());
  p.word(p.precision());
  p.word(p.period());
  p.word(p.left());
  p.word(p.right());
  p.word(static_cast<std::uint64_t>(p.clock()));
}
template <class Probe> void scenarios(Probe &p) {
  constexpr std::array<unsigned, 7> bases{0x1000, 0x1ff8,   0x1ffc,  0x2000,
                                          0xfff8, 0xfffff8, 0xffffff};
  constexpr std::array<unsigned, 9> lengths{0, 1, 7, 8, 15, 16, 24, 0x3fff8, 0xffffffff};
  constexpr std::array<unsigned, 6> rates{0, 1, 1103, 0x3ffe, 0x3fff, 0xffffffff};
  for (unsigned base : {0u, 1u, 2u, 4u, 5u, 6u})
    for (unsigned length : {0u, 2u, 3u, 4u, 6u, 7u, 8u})
      for (unsigned rate = 0; rate < rates.size(); ++rate)
        for (unsigned action = 0; action < 8; ++action) {
          p.reset();
          p.write(16, rates[rate]);
          p.write(20, action * 3 + 0xfffffff0u);
          p.write(0, bases[base]);
          p.write(4, lengths[length]);
          p.write(12, 0);
          p.write(0, 0x5ff8);
          p.write(4, action & 1 ? 0 : 16);
          p.write(0, 0x7000);
          p.write(4, 32);
          p.write(8, 1);
          observe(p);
          for (unsigned n = 0; n < 12; ++n) {
            if (n == 2 && action == 0)
              p.write(8, 0);
            if (n == 4 && action == 0)
              p.write(8, 1);
            if (n == 2 && action == 1)
              p.write(12, 0xffffffff);
            if (n == 2 && action == 2) {
              p.write(0, 0x3ffb);
              p.write(4, 15);
            }
            if (n == 2 && action == 3)
              p.write(16, 1103);
            if (n == 2 && action == 4)
              p.write(16, 0xffffffff);
            if (n == 2 && action == 5)
              p.power();
            if (n == 2 && action == 6)
              p.write(20, 0xffffffff);
            if (n == 2 && action == 7)
              p.write(8, 0xffffffff);
            p.sample();
            observe(p);
          }
          p.finish();
        }
  for (unsigned rate : rates)
    for (unsigned boundary : {0u, 1u, 2u, 7u})
      for (unsigned action = 0; action < 4; ++action) {
        p.reset();
        p.write(16, rate);
        p.write(0, 0x1ffb);
        p.write(4, 16);
        p.write(0, 0x5ffb);
        p.write(4, 8);
        p.write(8, 1);
        observe(p);
        const auto clocks = p.period() * boundary;
        p.advance(clocks ? clocks - 1 : 0);
        observe(p);
        if (action == 0)
          p.write(12, 0);
        if (action == 1)
          p.write(8, 0);
        if (action == 2)
          p.write(16, 1103);
        if (action == 3)
          p.power();
        p.advance(1);
        observe(p);
        p.advance(1);
        observe(p);
        p.finish();
      }
}
} // namespace test::audio_dma
