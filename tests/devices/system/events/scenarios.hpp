#pragma once
#include "../reset/immediate/scenarios.hpp"
#include <array>
#include <cstdint>
namespace test::cpu_events {
constexpr unsigned Observations = 37;
template <class Probe> void observe(Probe &p) {
  p.word(p.elapsed());
  p.word(p.pc());
  for (unsigned reg = 0; reg < 32; ++reg)
    p.word(p.gpr(reg));
  for (unsigned reg : {9u, 11u, 12u, 13u, 14u, 30u})
    p.word(p.control(reg));
  for (const auto range : std::array<std::array<unsigned, 2>, 6>{{{0x04300000, 4},
                                                                  {0x04400000, 14},
                                                                  {0x04500000, 6},
                                                                  {0x04600000, 13},
                                                                  {0x04800000, 7},
                                                                  {0x04040000, 8}}})
    for (unsigned n = 0; n < range[1]; ++n)
      p.word(p.read(range[0] + n * 4));
  p.word(p.video_clock());
  p.word(p.video_fraction());
  p.word(p.audio_clock());
  p.word(p.audio_left());
  p.word(p.audio_right());
  p.word(p.rsp_clock());
  p.word(p.rsp_dma_clock());
  for (unsigned n = 0; n < 64; n += 8)
    p.word(p.pif_memory(n));
  for (unsigned n = 0x3000; n < 0x3040; n += 8)
    p.word(p.raw_memory(n));
  for (unsigned n = 0x5000; n < 0x5040; n += 8)
    p.word(p.raw_memory(n));
  for (unsigned n = 0; n < 64; n += 8)
    p.word(p.rsp_memory(n));
}
template <class Probe> void scenarios(Probe &p) {
  for (bool native : {false, true})
    for (unsigned mask = 0; mask < 4u; ++mask)
      for (unsigned target : {0u, 1u, 4u, 24u, 56u, 64u})
        for (unsigned action = 0; action < 5u; ++action) {
          p.reset(false);
          test::machine_reset::initialize(p);
          p.fill(0);
          p.prepare_pif();
          for (unsigned n = 0; n < 64; n += 4)
            p.write_rsp(0x1000 + n, n == 8 ? 13u : 0u);
          p.code(0x1000, 0x25080001);
          p.code(0x1004, 0x1509fffe);
          p.code(0x1008, 0);
          p.code(0x180, 0x1000ffff);
          p.code(0x184, 0);
          p.setup_cpu(native, mask, target);
          p.write(0x04400000, 2);
          p.write(0x04400018, 8);
          p.write(0x0440001c, 7);
          p.write(0x04400020, 7 | 7u << 16);
          p.write(0x04400028, 1u << 17);
          p.write(0x0440000c, 2);
          p.write(0x04400010, 0);
          p.write(0x04800018, 0);
          p.write(0x0450000c, 0);
          p.write(0x04600010, 2);
          p.write(0x04040010, 8);
          p.write(0x04300000, 0x800);
          p.write(0x0430000c, mask == 0 ? 0x555 : mask == 1 ? 0xaaa : mask == 2 ? 0x2aa : 0x68a);
          if (action == 0 || action == 4) {
            p.write(0x04600000, 0x3000);
            p.write(0x04600004, 0x10000000);
            p.write(0x0460000c, 63);
          }
          if (action == 1 || action == 4) {
            p.write(0x04800000, 0x5000);
            p.write(0x04800004, 0x1fc007c0);
          }
          if (action == 2 || action == 4) {
            p.write(0x04500010, 0);
            p.write(0x04500000, 0x1ff8);
            p.write(0x04500004, 16);
            p.write(0x04500000, 0x5ff8);
            p.write(0x04500004, 8);
            p.write(0x04500008, 1);
          }
          if (action == 3 || action == 4) {
            p.write(0x04040000, 0);
            p.write(0x04040004, 0x2000);
            p.write(0x04040008, 31);
          }
          test::cpu_events::observe(p);
          for (unsigned boundary = 0; boundary < 32; ++boundary) {
            p.run(native, 128);
            test::cpu_events::observe(p);
          }
          p.run_until(native, 65536);
          test::cpu_events::observe(p);
          p.write(0x04800018, 0);
          p.write(0x0450000c, 0);
          p.write(0x04600010, 2);
          p.write(0x04400010, 0);
          p.write(0x04040010, 8);
          p.write(0x04300000, 0x800);
          p.write_compare(0);
          test::cpu_events::observe(p);
          p.run_until(native, p.elapsed() + 16);
          test::cpu_events::observe(p);
          p.run_until(native, p.elapsed() + 128);
          test::cpu_events::observe(p);
          p.finish();
        }
}
} // namespace test::cpu_events
