#pragma once
#include <array>
namespace test::cpu_flash {
constexpr unsigned FlashBytes = 131072, RamBytes = 0x4000;
constexpr unsigned WriteCode = 0x7000, ReadCode = 0x7040, Stop = 0x7800;
constexpr std::array<std::uint32_t, 3> write_code{0xac220000, 0x03e00008, 0};
constexpr std::array<std::uint32_t, 3> read_code{0x8c220000, 0x03e00008, 0};
constexpr std::uint8_t pattern(unsigned address, unsigned seed) {
  return static_cast<std::uint8_t>(address * 53 + (address >> 7) * 17 + seed);
}
constexpr unsigned duration(unsigned model, unsigned operation) {
  return operation == 0 ? (model == 6 ? 56250 : 656250)
         : model == 6   ? (operation == 1 ? 52500000 : 56250000)
                        : 15937500;
}
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
  p.install();
}
template <class P> void wait_pi(P &p, unsigned mask) {
  for (unsigned call = 0; call < 10000; ++call) {
    if (!(p.access(false, 0x04600010, 0) & mask))
      return;
    p.idle(512);
  }
  throw std::runtime_error("CPU Flash PI completion did not arrive");
}
template <class P> void command(P &p, unsigned value) {
  p.access(true, 0x08010000, value);
  wait_pi(p, 2);
}
template <class P> void dma(P &p, bool to_flash, unsigned address, unsigned length) {
  p.access(true, 0x04600000, address);
  p.access(true, 0x04600004, 0x08000000);
  p.access(true, to_flash ? 0x04600008 : 0x0460000c, length);
}
template <class P> void observe(P &p, unsigned phase, bool memory) {
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
  p.word(memory);
  if (memory) {
    for (unsigned n = 0; n < RamBytes; n += 8)
      p.word(p.ram_word(n));
    for (unsigned n = 0; n < RamBytes / 2; n += 8)
      p.word(p.coverage_word(n));
    for (unsigned n = 0; n < FlashBytes; n += 8)
      p.word(p.flash_word(n));
  }
}
template <class P> void scenarios(P &p) {
  for (unsigned model = 0; model < 7u; ++model)
    for (bool native : {false, true})
      for (unsigned operation = 0; operation < 3; ++operation)
        for (unsigned page_index = 0; page_index < 4u; ++page_index)
          for (unsigned boundary = 0; boundary < 5u; ++boundary)
            for (bool warm : {false, true}) {
              constexpr std::array<unsigned, 4> pages{0, 127, 128, 1023};
              constexpr std::array<unsigned, 4> lengths{0, 7, 127, 128};
              const auto page = pages[page_index];
              const auto length = lengths[(page_index + boundary) & 3];
              p.reset(model, native);
              initialize(p);
              p.fill();
              p.access(true, 0x0430000c, 0x200);
              for (unsigned n = 0; n < 4; ++n)
                p.access(true, 0x04600024 + n * 4,
                         boundary & 1 ? std::array<unsigned, 4>{0xfe, 0xff, 15, 3}[n] : 0);
              p.access(true, 0x04600010, 2);
              observe(p, 0, true);
              command(p, 0xe1000000);
              p.word(p.access(false, 0x08000000, 0));
              command(p, 0xb4000000);
              dma(p, true, 0x1000 + (page_index & 1 ? 6 : 0), length);
              observe(p, 1, false);
              wait_pi(p, 1);
              observe(p, 2, false);
              p.access(true, 0x04600010, 2);
              command(p, operation == 0   ? 0xa5000000 | page
                         : operation == 1 ? 0x4b000000 | page
                                          : 0x3c000000);
              if (operation != 0)
                command(p, 0x78000000);
              observe(p, 3, true);
              const auto clocks = duration(model, operation);
              const std::array<unsigned, 5> pauses{0, 1, clocks - 1, clocks, clocks + 1};
              p.idle(pauses[boundary]);
              command(p, 0xd2000000);
              p.word(p.access(false, 0x08000000, 0));
              dma(p, false, 0x2000 + (page_index & 1 ? 6 : 0), 127);
              observe(p, 4, false);
              p.power(warm);
              observe(p, 5, true);
              initialize(p);
              p.idle(clocks + 1);
              command(p, 0xe1000000);
              p.word(p.access(false, 0x08000000, 0));
              observe(p, 6, false);
              p.finish();
            }
}
} // namespace test::cpu_flash
