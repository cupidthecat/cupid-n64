#pragma once
#include <array>
#include <cstdint>
namespace test::rsp_dma {
constexpr std::uint32_t addiu(unsigned reg, unsigned value) {
  return 0x24000000u | reg << 21 | reg << 16 | value;
}
constexpr std::uint32_t mtc0(unsigned reg, unsigned control) {
  return 0x40800000u | reg << 16 | control << 11;
}
template <class P> void setup(P &p, bool native = false, unsigned salt = 0) {
  p.begin(native);
  for (unsigned reg = 1; reg < 32; ++reg)
    p.set_reg(reg, 0x918b27f3u + reg * 0x197fu + salt);
  for (unsigned reg = 0; reg < 32; ++reg)
    for (unsigned lane = 0; lane < 8; ++lane)
      p.set_vector(reg, lane, 0x9f17u + reg * 0x7a31u + lane * 0x217fu + salt);
  for (unsigned lane = 0; lane < 8; ++lane)
    p.set_accumulator(lane, 0x79a1be03c45full + lane * 0x7123781ull + salt);
  p.set_flags(0xa59c3f71b2ull, 0x9187, 0x2319, true);
  for (unsigned a = 0; a < 8192; a += 8) {
    const auto value = (std::uint64_t(0x934b217fu ^ (a * 0x10204081u) ^ salt) << 32) |
                       (0x17ac983du + a * 0x19fb5u + salt);
    p.local_write(a, value);
    p.store(a, value ^ 0xf0e1d2c3b4a59687ull);
    p.store(0x7fe000 + a, value ^ 0x0123456789abcdefull);
  }
}
template <class P> void snapshot(P &p) {
  for (unsigned reg = 0; reg < 7; ++reg)
    p.emit(p.io_read(reg * 4));
  p.emit(static_cast<std::uint64_t>(p.dma_clock()));
  p.emit(static_cast<std::uint64_t>(p.clock()));
  p.emit(p.pc());
  for (unsigned a = 0; a < 8192; a += 8)
    p.emit(p.local_read(a));
  for (unsigned a = 0; a < 8192; a += 8) {
    p.emit(p.raw(a));
    p.emit(p.raw(0x7fe000 + a));
  }
  p.registers();
  p.watched_ram();
}
template <class P>
void queue(P &p, bool write, unsigned local, unsigned dram, unsigned descriptor,
           int difference = 0) {
  p.io_write(0, local, difference);
  p.io_write(4, dram, difference);
  p.io_write(write ? 12 : 8, descriptor, difference);
  const auto length = (descriptor & 0xff8) + 8;
  const auto skip = (descriptor >> 20) & 0xff8;
  for (unsigned row = 0; row <= ((descriptor >> 12) & 255); ++row)
    for (unsigned offset = 0; offset < length; offset += 8)
      p.watch_ram(((dram & 0x00fffff8) + row * (length + skip) + offset) & 0x00ffffff);
}
template <class P>
void row_case(P &p, bool write, unsigned local, unsigned dram, unsigned descriptor,
              int difference) {
  setup(p, false, descriptor ^ local);
  queue(p, write, local, dram, descriptor, difference);
  snapshot(p);
  const auto duration = ((descriptor & 0xff8) + 8) / 8 * 3;
  for (unsigned row = 0; row <= ((descriptor >> 12) & 255); ++row) {
    const auto remaining = -p.dma_clock();
    if (remaining > 0)
      p.tick_dma(static_cast<unsigned>(remaining - 1));
    if (row == 0)
      snapshot(p);
    p.tick_dma(1);
    if (row < 2 || row == ((descriptor >> 12) & 255))
      snapshot(p);
    p.tick_dma(0);
  }
  p.tick_dma(duration + 999);
  snapshot(p);
  p.finish();
}
template <class P> void scenarios(P &p) {
  constexpr std::array lengths{0u, 1u, 7u, 8u, 15u, 31u, 248u, 255u, 4087u, 4088u, 4095u};
  constexpr std::array locals{0u, 7u, 8u, 0xff7u, 0xff8u, 0xfffu};
  constexpr std::array differences{-4096, -17, -1, 0, 1, 17, 4096};
  for (bool write : {false, true})
    for (unsigned region : {0u, 0x1000u})
      for (auto length : lengths)
        for (auto local : locals)
          for (auto difference : differences)
            row_case(p, write, region | local, (local & 8) ? 0xfffffdu : 0x105u, length,
                     difference);
  for (bool write : {false, true})
    for (unsigned region : {0u, 0x1000u})
      for (unsigned count : {1u, 2u, 15u, 255u})
        for (unsigned skip : {0u, 7u, 8u, 255u, 4088u, 4095u})
          row_case(p, write, region | 0xffb, 0xffffe7u, 15u | count << 12 | skip << 20, -17);
  for (unsigned variant = 0; variant < 64; ++variant) {
    setup(p, false, variant);
    queue(p, variant & 1, (variant & 2) ? 0x1ff8 : 0xff8, 0x108, 24 | 2 << 12, -17);
    snapshot(p);
    queue(p, variant & 4, (variant & 8) ? 0x1000 : 0, 0x200, 8 | 1 << 12 | 16 << 20);
    snapshot(p);
    if (variant & 16)
      queue(p, variant & 32, 0x1ff8, 0x308, 0);
    snapshot(p);
    for (unsigned ticks : {0u, 1u, 27u, 1u, 4096u, 4096u, 4096u, 4096u, 4096u}) {
      p.tick_dma(ticks);
      snapshot(p);
    }
    p.finish();
  }
  for (bool native : {false, true})
    for (unsigned start : {0u, 4u, 0xfe8u, 0xff8u})
      for (unsigned length : {0u, 8u, 24u, 248u})
        for (unsigned budget : {1u, 3u, 11u, 31u, 128u, 4096u}) {
          setup(p, native, start ^ length);
          for (unsigned a = 0; a < 4096; a += 4)
            p.code(a, 0);
          p.code(start, addiu(1, 1));
          p.code(start + 4, 0x4a000037u);
          p.code(start + 8, 0x08000000u | (start >> 2));
          p.code(start + 12, addiu(2, 1));
          p.set_pc(start);
          p.io_write(16, 1);
          p.run(32);
          snapshot(p);
          for (unsigned a = 0; a <= length; a += 8)
            p.store(0x100 + a, std::uint64_t(addiu(1, 7)) << 32 | 13u);
          queue(p, false, 0x1000 | start, 0x100, length);
          snapshot(p);
          p.run(budget);
          snapshot(p);
          p.run(8192);
          snapshot(p);
          p.set_pc(start);
          p.io_write(16, 5);
          p.run(1024);
          snapshot(p);
          p.finish();
        }
  for (bool native : {false, true})
    for (unsigned start : {0u, 4u, 0xff0u, 0xffcu})
      for (unsigned difference : {0u, 1u, 3u, 17u, 128u}) {
        setup(p, native, start);
        for (unsigned a = 0; a < 4096; a += 4)
          p.code(a, 0);
        p.code(start, mtc0(1, 0));
        p.code(start + 4, mtc0(2, 1));
        p.code(start + 8, mtc0(3, 2));
        p.code(start + 12, 0x08000000 | (((start + 20) & 0xfff) >> 2));
        p.code(start + 16, addiu(4, 1));
        p.code(start + 20, 13);
        p.set_reg(1, 0x1ff8);
        p.set_reg(2, 0x100);
        p.set_reg(3, 0);
        p.store(0x100, std::uint64_t(addiu(5, 9)) << 32 | 13u);
        p.set_pc(start);
        p.io_write(16, 1);
        p.run(difference);
        snapshot(p);
        p.run(256);
        snapshot(p);
        p.set_pc(0xff8);
        p.io_write(16, 5);
        p.run(128);
        snapshot(p);
        p.finish();
      }
  for (bool native : {false, true})
    for (unsigned start : {0u, 4u, 0xff8u, 0xffcu})
      for (unsigned variant = 0; variant < 4; ++variant) {
        setup(p, native, start);
        for (unsigned a = 0; a < 4096; a += 4)
          p.code(a, 0);
        p.code(start, addiu(1, 1));
        p.code(start + 4, 0x4a000037u);
        p.code(start + 8, 0x08000000u | (start >> 2));
        p.code(start + 12, addiu(2, 1));
        p.set_pc(start);
        p.io_write(16, 1);
        p.run(32);
        snapshot(p);
        const auto local = variant == 2 ? 0x1000 | ((start + 0x100) & 0xff8)
                                        : (variant == 3 ? 0u : 0x1000u) | (start & 0xff8);
        p.store(0x100, variant == 2 ? 0x240700030000000dull : p.local_read(local));
        if (variant)
          queue(p, false, local, 0x100, 0);
        snapshot(p);
        for (unsigned clocks : {1u, 3u, 11u, 128u, 4096u}) {
          p.run(clocks);
          snapshot(p);
        }
        p.finish();
      }
  for (bool write : {false, true})
    for (unsigned region : {0u, 0x1000u})
      for (unsigned length : {0u, 248u, 4088u})
        for (bool reset : {false, true}) {
          setup(p);
          queue(p, write, region | 0xff8, 0xfffff8, length | 3 << 12, -17);
          queue(p, !write, region, 0x100, 8);
          snapshot(p);
          for (unsigned clocks : {1u, 127u, 1u, 255u, 4096u}) {
            p.run(clocks);
            snapshot(p);
          }
          if (reset)
            p.begin(false);
          p.run(65535);
          snapshot(p);
          p.finish();
        }
}
} // namespace test::rsp_dma
