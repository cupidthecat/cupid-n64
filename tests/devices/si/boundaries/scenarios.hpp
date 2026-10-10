#pragma once

#include <array>
#include <cstdint>

namespace test::si_dma {
constexpr unsigned MemoryBytes = 0x4000;
constexpr std::uint8_t pattern(unsigned address) {
  return static_cast<std::uint8_t>(address * 37 + (address >> 6) + 0x51);
}
constexpr std::uint8_t packet(unsigned address) {
  return address == 0 ? 0xfe : address == 63 ? 0 : pattern(address + 0x10000);
}
template <class Probe> void observe(Probe &p) {
  for (unsigned offset = 0; offset < 32; offset += 4)
    p.emit(p.read(offset));
  p.emit(p.interrupts());
  p.emit(static_cast<std::uint32_t>(p.deadline()));
  p.emit(p.events.size());
  for (unsigned n = 0; n < 8; ++n)
    p.emit(n < p.events.size() ? p.events[n] : 0);
  p.emit(p.state());
  p.emit(p.reset_enabled());
  p.emit(p.timing());
  for (unsigned address = 0; address < 64; address += 8)
    p.emit(p.pif_memory(address));
  for (unsigned address = 0; address < MemoryBytes; address += 8)
    p.emit(p.memory(address));
  for (unsigned address = 0; address < MemoryBytes / 2; address += 8)
    p.emit(p.coverage(address));
}
template <class Probe> void scenarios(Probe &p) {
  constexpr std::array<unsigned, 3> bases{0x1000, 0x17fc, 0x1ff8};
  constexpr std::array<unsigned, 4> offsets{0, 0x7bc, 0x7c1, 0x7fc};
  for (unsigned direction = 0; direction < 2; ++direction)
    for (unsigned alias = 0; alias < 2; ++alias)
      for (unsigned base = 0; base < bases.size(); ++base)
        for (unsigned misalignment : {0u, 7u})
          for (unsigned offset = 0; offset < offsets.size(); ++offset) {
            const auto dram = bases[base] + misalignment;
            const auto bus = (alias ? 0x1fc00800u : 0x1fc00000u) + offsets[offset];
            p.reset(0);
            p.write(0, dram);
            p.write(direction ? 16 : 4, bus);
            observe(p);
            const auto deadline = static_cast<unsigned>(p.deadline());
            p.advance(deadline - 1);
            observe(p);
            p.advance(1);
            observe(p);
            p.write(24, 0xffffffff);
            p.emit(p.read(24));
            p.emit(p.interrupts());
            p.finish();
          }
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (unsigned action = 0; action < 8; ++action)
      for (unsigned direction = 0; direction < 2; ++direction) {
        p.reset(epoch);
        p.write(0, 0x17fb);
        p.write(direction ? 16 : 4, 0x1fc007c1);
        observe(p);
        if (action == 0)
          p.write(0, 0x1ffb);
        if (action == 1)
          p.write(direction ? 16 : 4, 0x1fc007e1);
        if (action == 2)
          p.write(direction ? 4 : 16, 0x1fc007c1);
        if (action == 3)
          p.write(24, 0xffffffff);
        if (action == 4)
          p.bus_write(0x1fc007c4, 4, 0x12345678);
        if (action == 5) {
          p.bus_write(0x1fc007c4, 4, 0x12345678);
          p.emit(p.bus_read(0x1fc007c8, 4));
        }
        if (action == 6 || action == 7)
          p.power();
        observe(p);
        p.advance(2000000);
        observe(p);
        p.finish();
      }
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (unsigned action = 0; action < 6; ++action)
      for (unsigned bytes : {1u, 2u, 4u, 8u}) {
        p.reset(epoch);
        p.bus_write(0x1fc007c0, bytes, 0x12345678deadbeefull);
        observe(p);
        if (action == 0)
          p.bus_write(0x1fc007c4, bytes, 0xabcdef019abcdef0ull);
        if (action == 1)
          p.emit(p.bus_read(0x1fc007c8, bytes));
        if (action == 2)
          p.write(24, 0xffffffff);
        if (action == 3) {
          p.emit(p.bus_read(0x1fc007c8, bytes));
          p.bus_write(0x1fc007c4, bytes, 0xabcdef019abcdef0ull);
        }
        if (action == 4 || action == 5)
          p.power();
        observe(p);
        p.advance(6449);
        observe(p);
        p.advance(1);
        observe(p);
        p.advance(1);
        observe(p);
        p.finish();
      }
  for (unsigned channel = 0; channel < 5; ++channel)
    for (unsigned send : {0u, 1u, 63u, 0x40u, 0xc0u, 0xfdu, 0xfeu})
      for (unsigned receive : {0u, 3u, 63u, 0xc0u})
        for (unsigned padding : {0u, 50u}) {
          p.reset(0);
          p.prepare_command(channel, send, receive, padding);
          p.write(0, 0x1007);
          p.write(4, 0x1fc007c1);
          observe(p);
          const auto deadline = static_cast<unsigned>(p.deadline());
          p.advance(deadline - 1);
          observe(p);
          p.advance(1);
          observe(p);
          p.finish();
        }
}
} // namespace test::si_dma
