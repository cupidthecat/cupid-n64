#pragma once

#include <array>
#include <cstdint>

namespace test::pif_boot {

constexpr unsigned TickClocks = 81920;
constexpr unsigned TimeoutClocks = 1125000000;
constexpr std::array<std::uint64_t, 15> Checksums{
    0x93e983a8f152ull, 0x45cc73ee317aull, 0xa536c0f1d859ull, 0xa536c0f1d859ull, 0x44160ec5d9afull,
    0x586fd4709867ull, 0x586fd4709867ull, 0x8618a45bc2d3ull, 0x8618a45bc2d3ull, 0x2bbad4e6eb74ull,
    0x2bbad4e6eb74ull, 0x32b294e2ab90ull, 0x6ee8d9e84970ull, 0x083c6c77e0b1ull, 0x05ba2ef0a5f1ull};
constexpr std::uint8_t firmware(unsigned address) {
  return static_cast<std::uint8_t>(address * 41 + (address >> 5) + 0x67);
}
template <class Probe> void observe(Probe &p) {
  p.emit(p.state());
  p.emit(p.reset_enabled());
  p.emit(p.timing());
  for (unsigned n = 0; n < 64; n += 8)
    p.emit(p.memory(n));
  for (unsigned n = 0; n < 64; n += 8)
    p.emit(p.dma_memory(n));
  for (unsigned address : {0u, 4u, 0x7bcu})
    p.emit(p.read(address));
  p.emit(p.poll());
  p.emit(p.pc());
  p.emit(p.status());
  p.emit(p.error_epc());
  p.emit(p.cpu_clocks());
}
template <class Probe> void scenarios(Probe &p) {
  for (unsigned model = 0; model < Checksums.size(); ++model)
    for (unsigned action = 0; action < 9; ++action) {
      p.reset(model);
      observe(p);
      p.tick();
      observe(p);
      p.write(0x7fc, 0x10);
      observe(p);
      const auto checksum = Checksums[model] ^ (action == 1 ? 1ull : 0ull);
      p.write(0x7f0, static_cast<unsigned>(checksum >> 32));
      p.write(0x7f4, static_cast<unsigned>(checksum));
      p.write(0x7fc, 0x20);
      observe(p);
      p.write(0x7fc, 0x40);
      observe(p);
      p.align(pif_boot::TickClocks * 6);
      if (action == 0 || action == 1)
        p.write(0x7fc, 8);
      if (action == 0)
        p.challenge(model + 1);
      if (action == 2) {
        for (unsigned n = 0; n < 13732; ++n)
          p.tick();
        observe(p);
        p.tick();
      }
      if (action == 3 || action == 4) {
        p.advance(TimeoutClocks - (action == 3 ? 1u : 0u));
        observe(p);
        p.advance(1);
      }
      if (action == 5 || action == 6) {
        p.power();
        observe(p);
        p.tick();
      }
      if (action == 7) {
        p.write(0x7fc, 0);
        p.tick();
      }
      if (action == 8) {
        p.advance(TimeoutClocks - TickClocks);
        p.write(0x7fc, 8);
      }
      observe(p);
      p.tick();
      observe(p);
      p.tick();
      observe(p);
      p.finish();
    }
  for (unsigned model = 0; model < Checksums.size(); ++model)
    for (unsigned seed : {0u, 1u, 15u, 31u}) {
      p.reset(model);
      p.tick();
      p.write(0x7fc, 0x10);
      p.write(0x7f0, static_cast<unsigned>(Checksums[model] >> 32));
      p.write(0x7f4, static_cast<unsigned>(Checksums[model]));
      p.write(0x7fc, 0x20);
      p.write(0x7fc, 0x40);
      p.write(0x7fc, 8);
      observe(p);
      p.challenge(seed);
      observe(p);
      p.challenge(seed ^ 31);
      observe(p);
      p.finish();
    }
  for (unsigned model = 0; model < Checksums.size(); ++model)
    for (unsigned tick : {13732u, 13733u})
      for (unsigned offset : {0u, 1u})
        for (unsigned terminate = 0; terminate < 2; ++terminate) {
          p.reset(model);
          p.tick();
          p.write(0x7fc, 0x10);
          p.write(0x7f0, static_cast<unsigned>(Checksums[model] >> 32));
          p.write(0x7f4, static_cast<unsigned>(Checksums[model]));
          p.write(0x7fc, 0x20);
          p.write(0x7fc, 0x40);
          p.align(TickClocks * 6);
          observe(p);
          p.advance(tick * TickClocks + offset);
          observe(p);
          if (terminate)
            p.write(0x7fc, 8);
          else
            p.tick();
          observe(p);
          p.tick();
          observe(p);
          p.finish();
        }
}
} // namespace test::pif_boot
