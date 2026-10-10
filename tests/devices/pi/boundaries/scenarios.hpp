#pragma once

#include <array>
#include <cstdint>

namespace test::pi_dma {

constexpr unsigned MemoryBytes = 0x4000, CartridgeBytes = 0x4000;
constexpr std::uint8_t ram_pattern(unsigned address) {
  return static_cast<std::uint8_t>(address * 29 + (address >> 7) + 0x53);
}
constexpr std::uint8_t cartridge_pattern(unsigned address) {
  return static_cast<std::uint8_t>(address * 43 + (address >> 5) + 0xa7);
}

template <class Probe> void observe(Probe &p) {
  for (unsigned offset = 0; offset < 64; offset += 4)
    p.emit(p.read(offset));
  p.emit(p.interrupts());
  p.emit(static_cast<std::uint32_t>(p.deadline()));
  p.emit(p.events.size());
  for (unsigned n = 0; n < 8; ++n)
    p.emit(n < p.events.size() ? p.events[n] : 0);
  for (unsigned address = 0; address < MemoryBytes; address += 8)
    p.emit(p.memory(address));
  for (unsigned address = 0; address < MemoryBytes / 2; address += 8)
    p.emit(p.coverage(address));
  for (unsigned address = 0; address < CartridgeBytes; address += 8)
    p.emit(p.cartridge(address));
}

template <class Probe> void scenarios(Probe &p) {
  constexpr std::array<unsigned, 5> lengths{0, 7, 127, 128, 2048};
  constexpr std::array<unsigned, 3> bases{0x1000, 0x17fe, 0x1ff8};
  constexpr std::array<unsigned, 3> offsets{0, 126, 0xffe};
  constexpr std::array<std::array<unsigned, 4>, 2> timings{std::array<unsigned, 4>{0, 0, 0, 0},
                                                           {0xfe, 0xff, 15, 3}};
  for (unsigned direction = 0; direction < 2; ++direction)
    for (unsigned domain = 0; domain < 2; ++domain)
      for (const auto &timing : timings)
        for (unsigned base : bases)
          for (unsigned misalignment : {0u, 6u})
            for (unsigned offset : offsets)
              for (unsigned length : lengths) {
                p.reset(0);
                const auto settings = domain ? 36u : 20u;
                for (unsigned n = 0; n < 4; ++n)
                  p.write(settings + n * 4, timing[n]);
                p.write(0, base + misalignment);
                p.write(4, (domain ? 0x08000000u : 0x10000000u) + offset);
                p.write(direction ? 8 : 12, length);
                observe(p);
                const auto deadline = static_cast<unsigned>(p.deadline());
                p.advance(deadline - 1);
                p.emit(p.read(16));
                p.emit(p.interrupts());
                p.emit(static_cast<std::uint32_t>(p.deadline()));
                p.advance(1);
                p.emit(p.read(16));
                p.emit(p.interrupts());
                p.emit(static_cast<std::uint32_t>(p.deadline()));
                p.emit(p.events.size());
                p.emit(p.events.empty() ? 0 : p.events.back());
                p.write(16, 2);
                p.emit(p.read(16));
                p.emit(p.interrupts());
                p.finish();
              }
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (unsigned action = 0; action < 5; ++action)
      for (unsigned direction = 0; direction < 2; ++direction) {
        p.reset(epoch);
        p.write(0, 0x17fa);
        p.write(4, 0x1000007e);
        p.write(direction ? 8 : 12, 128);
        const auto deadline = static_cast<unsigned>(p.deadline());
        if (action == 0)
          p.write(0, 0x2000);
        if (action == 1)
          p.write(16, 1);
        if (action == 2)
          p.write(16, 2);
        if (action >= 3)
          p.power();
        observe(p);
        p.advance(deadline + 1);
        observe(p);
        p.finish();
      }
}

} // namespace test::pi_dma
