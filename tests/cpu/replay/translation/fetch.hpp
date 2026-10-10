#pragma once
#include "scenarios.hpp"

namespace test::cpu_replay::translation::fetch {
constexpr std::array<std::uint64_t, 16> starts{0x60000,
                                               0x60001,
                                               0x60ffc,
                                               0x61000,
                                               0x61ffc,
                                               0x62000,
                                               0x4000000000060000ull,
                                               0x4000000000060ffCull,
                                               0xc000000000060000ull,
                                               0xc000000000060ffCull,
                                               0xffffffffc0060000ull,
                                               0xffffffffc0060ffCull,
                                               0xffffffff80004000ull,
                                               0xffffffff80004ffCull,
                                               0xffffffffa0004000ull,
                                               0xffffffffa0004ffCull};

template <class P>
void run(P &p, bool native, unsigned mode, bool boot, bool exl, unsigned variant, unsigned branch,
         bool warm, std::uint64_t start) {
  p.begin();
  for (unsigned reg = 0; reg < 32; ++reg) {
    p.gpr(reg, reg ? 0x879abcdef0000000ull + reg * 0x137 : 0);
    p.fpr(reg, 0xabcdef0000000000ull + reg * 0x197f);
  }
  p.hilo(0x12349876, 0x567abc98);
  p.control(6, 31);
  p.control(9, 0);
  p.control(11, 0xffffffff);
  p.control(13, 0);
  p.control(4, 0x1873abce10800000ull);
  p.control(20, 0xffabcdef00000000ull);
  p.control(14, 0x1234abc789);
  unsigned lo0 = 0x1f, lo1 = 0x1f;
  if (variant == 1)
    lo0 &= ~2u;
  if (variant == 2)
    lo1 &= ~2u;
  if (variant == 3)
    lo0 &= ~1u;
  if (variant == 4)
    lo1 &= ~1u;
  if (variant == 5)
    lo0 = lo1 = 0x17;
  if (variant == 6)
    lo0 = lo1 = 0x1b;
  if (variant != 7)
    translation::entry(p, 2, (start & 0xc000000000000000ull) | 0x60035, 0, (0x4000 >> 6) | lo0,
                       (0x8000 >> 6) | lo1);
  const std::uint64_t hi =
      (start & 0xc000000000000000ull) | 0x60000 | (variant == 3 || variant == 4 ? 0x36 : 0x35);
  const unsigned offset = static_cast<unsigned>(start) & 0xffc;
  for (unsigned base : {0x4000u, 0x5000u, 0x8000u, 0x9000u})
    for (unsigned word = 0; word < 8; ++word)
      p.physical_write(base + word * 4, 0);
  for (unsigned base : {0x4000u, 0x8000u})
    for (unsigned word = 0; word < 8; ++word)
      p.physical_write(base + offset + word * 4, 0);
  const unsigned first = branch == 0   ? 0x24420001u
                         : branch == 1 ? 0x10000003u
                         : branch == 2 ? 0x54000003u
                                       : 0x14000003u;
  const auto write_code = [&](unsigned physical) {
    p.physical_write(physical, first);
    p.physical_write(physical + 4, 0x24630001);
    p.physical_write(physical + 8, 0x24840001);
    p.physical_write(physical + 12, 26u << 21 | 8u);
    p.physical_write(physical + 16, 0x24a50001);
  };
  write_code(0x4000 + offset);
  write_code(0x8000 + offset);
  if (offset == 0xffc)
    write_code(0x7ffc);
  p.gpr(26, start + 0x100);
  p.control(10, hi);
  p.control(12, mode | (boot ? 0x00400000u : 0) | (exl ? 2u : 0));
  p.control(16, 0x7006e460);
  p.pc(start);
  if (warm) {
    p.single(native);
    p.control(10, hi);
    p.control(12, mode | (boot ? 0x00400000u : 0) | (exl ? 2u : 0));
    p.pc(start);
  }
  for (auto value :
       {std::uint64_t(native), std::uint64_t(mode), std::uint64_t(boot), std::uint64_t(exl),
        std::uint64_t(variant), std::uint64_t(branch), std::uint64_t(warm), start})
    p.emit(value);
  p.observe();
  for (unsigned base : {0x4000u, 0x8000u})
    for (unsigned word = 0; word < 8; ++word)
      p.emit(p.physical_read(base + offset + word * 4));
  for (unsigned step = 0; step < 3; ++step) {
    const bool active = p.current_pc() >= start && p.current_pc() < start + 32;
    p.emit(active);
    if (active)
      p.single(native);
    p.observe();
    p.control(10, hi);
    p.control(12, mode | (boot ? 0x00400000u : 0) | (exl ? 2u : 0));
    if (step == 1)
      p.pc(start);
  }
  translation::inspect(p);
  p.finish();
}

template <class P> void scenarios(P &p, unsigned routes) {
  for (unsigned route = 0; route < routes; ++route)
    for (auto mode : translation::modes)
      for (bool boot : {false, true})
        for (bool exl : {false, true})
          for (unsigned variant = 0; variant < 8; ++variant)
            for (unsigned branch = 0; branch < 4; ++branch)
              for (bool warm : {false, true})
                for (unsigned n = 0; n < starts.size(); ++n)
                  run(p, route != 0, mode, boot, exl, variant, branch, warm, starts[n]);
}
} // namespace test::cpu_replay::translation::fetch
