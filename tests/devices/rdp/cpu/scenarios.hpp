#pragma once
#include "../../system/cpu/program.hpp"

namespace test::cpu_rdp {
constexpr unsigned Packet = 0x3000;
constexpr std::array<std::uint32_t, 8> Commands{0x26000000, 0, 0x27000000, 0,
                                                0x28000000, 0, 0x29000000, 0};
template <class P> void packet(P &p, bool signal, unsigned address) {
  for (unsigned n = 0; n < Commands.size(); ++n)
    if (signal)
      p.signal_write((address + n * 4) & 0xfff, Commands[n]);
    else
      p.initial_write(address + n * 4, Commands[n]);
}
template <class P> void observe(P &p, unsigned phase) {
  p.word(phase);
  cpu_bus::observe_cpu(p);
  for (unsigned n = 0; n < 8; ++n)
    p.word(p.register_word(0x04100000 + n * 4));
  p.word(p.register_word(0x04300008));
  p.word(p.register_word(0x0430000c));
  for (unsigned n = 0; n < cpu_bus::RamBytes; n += 8)
    p.word(p.ram_word(n));
  for (unsigned n = 0; n < 0x1000; n += 8)
    p.word(p.signal_word(n));
}
template <class P> void scenarios(P &p) {
  for (bool native : {false, true})
    for (bool warm : {false, true})
      for (bool signal : {false, true})
        for (unsigned variant = 0; variant < 4; ++variant)
          for (bool masked : {false, true}) {
            p.reset(native);
            cpu_bus::initialize_memory(p);
            p.fill_ram();
            for (unsigned n = 0; n < 0x1000; n += 4)
              p.signal_write(n, 0xa5a5a5a5 ^ n);
            const auto address = signal ? (variant & 1 ? 0xff8 : 0x400) : Packet;
            packet(p, signal, address);
            p.access(true, 0x04300000, 0x800);
            p.access(true, 0x0430000c, masked ? 0 : 0x800);
            p.access(true, 0x0410000c, (signal ? 2 : 1) | (variant ? 8 : 0));
            observe(p, 0);
            p.access(true, 0x04100000, address);
            p.access(true, 0x04100004, address + (variant == 2 ? 16 : 32));
            observe(p, 1);
            p.word(p.access(false, 0x0410000c, 0));
            observe(p, 2);
            if (variant == 2)
              p.access(true, 0x04100004, address + 32);
            if (variant != 3)
              p.access(true, 0x0410000c, variant == 2 ? 12 : 4);
            observe(p, 3);
            p.word(p.access(false, 0x04300008, 0));
            p.access(true, 0x04300000, 0x800);
            observe(p, 4);
            p.power(warm);
            observe(p, 5);
            cpu_bus::initialize_memory(p);
            packet(p, signal, address);
            p.access(true, 0x04300000, 0x800);
            p.access(true, 0x0430000c, masked ? 0 : 0x800);
            p.access(true, 0x0410000c, signal ? 2 : 1);
            p.access(true, 0x04100000, address);
            p.access(true, 0x04100004, address + 32);
            observe(p, 6);
            p.word(p.access(false, 0x04100008, 0));
            p.access(true, 0x0430000c, 0x400);
            observe(p, 7);
            p.access(true, 0x04300000, 0x800);
            p.access(true, 0x0430000c, 0x800);
            observe(p, 8);
            p.finish();
          }
}
} // namespace test::cpu_rdp
