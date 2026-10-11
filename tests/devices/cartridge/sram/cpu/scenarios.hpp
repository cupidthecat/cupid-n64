#pragma once
#include "../../cpu/program.hpp"

namespace test::cpu_sram {
using namespace save_cpu;
template <class P> void wait(P &p, unsigned mask) {
  for (unsigned call = 0; call < 10000; ++call) {
    if (!(p.access(false, 0x04600010, 0) & mask))
      return;
    p.idle(512);
  }
  throw std::runtime_error("CPU SRAM completion did not arrive");
}
template <class P> void dma(P &p, bool write, unsigned ram, unsigned bus, unsigned length) {
  p.access(true, 0x04600000, ram);
  p.access(true, 0x04600004, bus);
  p.access(true, write ? 0x04600008 : 0x0460000c, length);
}
template <class P> void observe(P &p, unsigned phase, unsigned size) {
  p.word(phase);
  p.word(p.clock());
  p.word(p.pc());
  for (unsigned n = 0; n < 32; ++n)
    p.word(p.gpr(n));
  for (unsigned n : {9u, 11u, 12u, 13u, 16u, 30u})
    p.word(p.control(n));
  p.word(p.frozen());
  for (unsigned n = 0; n < 13; ++n)
    p.word(p.register_word(0x04600000 + n * 4));
  p.word(p.register_word(0x04300008));
  p.word(p.register_word(0x0430000c));
  for (unsigned n = 0; n < RamBytes; n += 8)
    p.word(p.ram_word(n));
  for (unsigned n = 0; n < RamBytes / 2; n += 8)
    p.word(p.coverage_word(n));
  for (unsigned n = 0; n < size; n += 8)
    p.word(p.save_word(n));
}
template <class P> void scenarios(P &p) {
  for (unsigned size : {512u, 32768u, 65536u, 98304u, 131072u})
    for (bool native : {false, true})
      for (bool warm : {false, true})
        for (unsigned variant = 0; variant < 4; ++variant)
          for (unsigned boundary = 0; boundary < 3; ++boundary) {
            p.reset(size, native);
            save_cpu::initialize_memory(p);
            p.fill(size);
            p.access(true, 0x0430000c, 0x200);
            for (unsigned n = 0; n < 4; ++n)
              p.access(true, 0x04600024 + n * 4,
                       variant & 1 ? std::array<unsigned, 4>{0xfe, 0xff, 15, 3}[n] : 0);
            const auto window = size > 32768 ? 32768 : size;
            const auto bank = size > 32768 ? (size / 32768 - 1) << 18 : 0;
            const std::array<unsigned, 4> addresses{0x08000000, 0x08000000 + bank + window - 4,
                                                    0x08000000 + bank + window,
                                                    0x08000000 + bank + 0x40000};
            const auto address = addresses[variant];
            observe(p, 0, size);
            p.word(p.access(true, address, 0x89abcdef ^ size ^ variant));
            observe(p, 1, size);
            wait(p, 2);
            p.word(p.access(false, address, 0));
            observe(p, 2, size);
            constexpr std::array lengths{0u, 7u, 127u, 128u};
            dma(p, true, 0x1000 + (variant & 1 ? 6 : 0), address, lengths[variant]);
            observe(p, 3, size);
            const auto deadline = p.deadline();
            if (!deadline || deadline == 0x7fffffff)
              throw std::runtime_error("CPU SRAM DMA deadline unavailable");
            p.idle(deadline - 1);
            observe(p, 4, size);
            p.idle(boundary);
            observe(p, 5, size);
            p.power(warm);
            observe(p, 6, size);
            save_cpu::initialize_memory(p);
            p.idle(deadline + 1);
            observe(p, 7, size);
            dma(p, false, 0x2000 + (variant & 1 ? 6 : 0), address, 127);
            wait(p, 1);
            p.access(true, 0x04600010, 2);
            p.word(p.access(false, address, 0));
            observe(p, 8, size);
            p.finish();
          }
}
} // namespace test::cpu_sram
