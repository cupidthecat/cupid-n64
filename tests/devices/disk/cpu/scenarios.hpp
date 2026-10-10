#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace test::disk_cpu {
template <class P> void initialize(P &p) {
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
  for (unsigned offset : {0u, 0x40u}) {
    p.initial_write(0x7000 + offset, offset ? 0x8c220000 : 0xac220000);
    p.initial_write(0x7004 + offset, 0x03e00008);
    p.initial_write(0x7008 + offset, 0);
  }
  p.control_write(12, 0x30000000);
  p.control_write(11, 0x10000000);
}

template <class P> void run(P &p) {
  for (unsigned mode = 0; mode < p.modes(); ++mode)
    for (unsigned pause : {0u, 100u, 399u, 400u, 401u, 7800u, 8000u})
      for (unsigned reset = 0; reset < 3; ++reset) {
        p.reset(mode);
        initialize(p);
        p.observe(pause, reset, 0);
        p.access(true, 0x05000508, 27u << 16);
        p.observe(pause, reset, 1);
        p.idle(pause);
        p.access(true, 0x05000500, 0x5a5a0000);
        const auto value = p.access(false, 0x05000500, 0);
        p.observe(pause, reset, 2, value);
        if (reset == 1)
          p.access(true, 0x05000520, 0xaaaa0000);
        else if (reset == 2) {
          p.power(true);
          initialize(p);
        }
        p.idle(8000);
        p.observe(pause, reset, 3);
        p.access(true, 0x05000510, 0x01000000);
        p.idle(400);
        const auto status = p.access(false, 0x05000508, 0);
        p.observe(pause, reset, 4, status);
      }
}
} // namespace test::disk_cpu
