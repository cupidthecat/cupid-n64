#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>

namespace test::cpu_replay::translation {
constexpr std::array modes{0x30000000u, 0x30000080u, 0x30000008u,
                           0x30000048u, 0x30000010u, 0x30000030u};
constexpr std::array masks{0u,        0x6000u,   0x1e000u,   0x7e000u,
                           0x1fe000u, 0x7fe000u, 0x1ffe000u, 0x1554000u};
constexpr std::array offsets{0xfffu,   0x3fffu,   0xffffu,   0x3ffffu,
                             0xfffffu, 0x3fffffu, 0xffffffu, 0xaaafffu};
constexpr std::array<std::uint64_t, 20> addresses{0x60000,
                                                  0x61000,
                                                  0x62000,
                                                  0x63ffc,
                                                  0x67ffc,
                                                  0x7fffc,
                                                  0x200000,
                                                  0x000000ffffffffffull,
                                                  0x0000010000000000ull,
                                                  0x4000000000060000ull,
                                                  0x400000ffffffffffull,
                                                  0x4000010000000000ull,
                                                  0x8000000000004000ull,
                                                  0x9000000000004000ull,
                                                  0x9000000100004000ull,
                                                  0xc000000000060000ull,
                                                  0xffffffff83efffffull,
                                                  0xffffffff83f00000ull,
                                                  0xffffffffc0060000ull,
                                                  0xffffffffe0060000ull};

template <class P>
void entry(P &p, unsigned index, std::uint64_t hi, unsigned mask, unsigned lo0, unsigned lo1) {
  p.control(12, 0x30000080);
  p.control(0, index);
  p.control(5, mask);
  p.control(10, hi);
  p.control(2, lo0);
  p.control(3, lo1);
  p.execute(0x42000002);
}

template <class P> void inspect(P &p) {
  p.control(12, 0x30000080);
  p.pc(0xffffffffa0001000ull);
  for (unsigned operation : {4u, 5u})
    for (unsigned index = 0; index < 512; ++index) {
      p.gpr(27, 0xffffffff80000000ull + index * (operation == 4 ? 32 : 16));
      p.execute(47u << 26 | 27u << 21 | operation << 16);
      p.emit(p.read_control(28));
      p.emit(p.read_control(29));
    }
  for (unsigned index = 0; index < 32; ++index) {
    p.control(0, index);
    p.execute(0x42000001);
    for (unsigned reg : {2u, 3u, 5u, 10u})
      p.emit(p.read_control(reg));
  }
  p.observe();
}

template <class P>
void run(P &p, unsigned route, unsigned mode, bool boot_vector, bool exl, bool delay, unsigned mask,
         unsigned variant, unsigned operation, std::uint64_t address) {
  p.begin();
  for (unsigned reg = 0; reg < 32; ++reg) {
    p.gpr(reg, reg ? 0x123456789abc0000ull + reg * 0x1973 : 0);
    p.fpr(reg, 0x4120000000000000ull + reg);
  }
  p.hilo(0x934bac2710, 0x239fd178);
  p.control(6, 31);
  p.control(9, 0);
  p.control(11, 0xffffffff);
  p.control(13, 0);
  p.control(4, 0x9876543210800000ull);
  p.control(20, 0xfedcba9800000000ull);
  p.control(14, 0x0123456789abcdefull);
  entry(p, 1, 0x10000000, 0, (0x1000 >> 6) | 0x1f, (0x2000 >> 6) | 0x1f);
  unsigned flags0 = 0x1f, flags1 = 0x1f;
  if (variant == 1)
    flags0 &= ~2u;
  if (variant == 2)
    flags1 &= ~2u;
  if (variant == 3)
    flags0 &= ~4u;
  if (variant == 4)
    flags1 &= ~4u;
  if (variant == 5)
    flags0 &= ~1u;
  if (variant == 6)
    flags1 &= ~1u;
  if (variant == 7)
    flags0 = flags1 = 0x17;
  const auto hi = (address & 0xc000000000000000ull) | 0x60000 | 0x35;
  if (variant != 8)
    entry(p, 2, hi, mask, (0x4000 >> 6) | flags0, (0x8000 >> 6) | flags1);
  p.control(10, (hi & ~0xffull) | (variant == 5 || variant == 6 ? 0x36 : 0x35));
  for (unsigned n = 0; n < 32; ++n) {
    p.physical_write(0x4000 + n * 4, 0x13429876u ^ n * 0x34567u);
    p.physical_write(0x8000 + n * 4, 0x987abcd0u ^ n * 0x197fu);
    p.code(n * 4, 0);
  }
  unsigned offset_mask = 0;
  for (unsigned n = 0; n < masks.size(); ++n)
    if (masks[n] == mask)
      offset_mask = offsets[n];
  const unsigned offset = static_cast<unsigned>(address & offset_mask) & ~3u;
  const unsigned physical = static_cast<unsigned>(address) & 0x1ffffffcu;
  const std::array locations{0x4000u + offset, 0x8000u + offset,
                             physical < 0x800000 ? physical : 0x1f000u};
  for (unsigned n = 0; n < locations.size(); ++n)
    if (locations[n] < 0x800000)
      p.physical_write(locations[n], 0x439abc12u ^ n * 0x879abc);
  const auto start = (mode & 0x18) && !exl ? 0x10000000ull : 0xffffffff80001000ull;
  const auto instruction = operation << 26 | 1u << 21 | 2u << 16;
  unsigned cursor = 0;
  if (delay)
    p.code(cursor++ * 4, 0x10000002);
  p.code(cursor++ * 4, instruction);
  p.code(cursor++ * 4, 26u << 21 | 8u);
  p.code(cursor++ * 4, 0);
  p.gpr(1, address);
  p.gpr(2, 0x0123456789abcdefull);
  p.gpr(26, start + 0x100);
  p.control(12, mode | (boot_vector ? 0x00400000u : 0) | (exl ? 2u : 0));
  p.control(16, 0x7006e460);
  p.pc(start);
  for (auto value : {std::uint64_t(route), std::uint64_t(mode), std::uint64_t(boot_vector),
                     std::uint64_t(exl), std::uint64_t(delay), std::uint64_t(mask),
                     std::uint64_t(variant), std::uint64_t(operation), address})
    p.emit(value);
  p.observe();
  for (unsigned n = 0; n < cursor; ++n)
    p.emit(p.word(n * 4));
  if (route == 0) {
    if (delay)
      p.execute(0x10000002);
    p.execute(instruction);
  } else
    p.run(route == 2, start, start + cursor * 4);
  p.observe();
  inspect(p);
  p.control(12, 0x30000080);
  p.pc(0xffffffffa0001000ull);
  for (unsigned location : locations) {
    const unsigned base = location < 0x800000 ? location : 0x1f000u;
    p.emit(base);
    p.emit(p.physical_read(base));
    p.gpr(27, 0xffffffff80000000ull + base);
    p.execute(35u << 26 | 27u << 21 | 28u << 16);
    p.emit(p.reg(28));
    p.execute(47u << 26 | 27u << 21 | 21u << 16);
    p.emit(p.physical_read(base));
  }
  for (unsigned n = 0; n < 32; ++n) {
    p.emit(p.physical_read(0x4000 + n * 4));
    p.emit(p.physical_read(0x8000 + n * 4));
  }
  p.observe();
  p.finish();
}

template <class P> void scenarios(P &p, unsigned routes) {
  for (unsigned route = 0; route < routes; ++route)
    for (auto mode : modes)
      for (bool boot : {false, true})
        for (bool exl : {false, true})
          for (bool delay : {false, true})
            for (auto mask : masks)
              for (unsigned variant = 0; variant < 9; ++variant)
                for (unsigned operation : {35u, 43u})
                  for (auto address : addresses)
                    run(p, route, mode, boot, exl, delay, mask, variant, operation, address);
}
} // namespace test::cpu_replay::translation
