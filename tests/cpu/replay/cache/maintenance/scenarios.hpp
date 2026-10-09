#pragma once
#include "../../decode/scenarios.hpp"
namespace test::cpu_replay::cache::maintenance {
constexpr std::array modes{0x30000000u, 0x30000080u, 0x30000008u,
                           0x30000048u, 0x30000010u, 0x30000030u};
constexpr std::array indices{0u, 127u, 128u, 511u};
template <class P>
void run(P &p, unsigned route, unsigned mode, unsigned region, bool little, bool reverse,
         bool delay, unsigned operation, unsigned initial, unsigned index) {
  mode |= reverse ? 0x02000000u : 0;
  const auto instruction = 47u << 26 | 1u << 21 | operation << 16;
  test::cpu_replay::decode::prepare(p, 0x30000080, initial, instruction);
  p.mapping(0x30000080);
  for (unsigned page = 0; page < 2; ++page) {
    p.control(0, page + 2);
    p.control(2, ((0x4000 + page * 0x2000) >> 6) | 0x1f);
    p.control(3, ((0x5000 + page * 0x2000) >> 6) | 0x1f);
    p.control(5, 0);
    p.control(10, 0x50000 + page * 0x2000);
    p.execute(0x42000002);
  }
  const unsigned physical = 0x4000 + index * 32;
  const std::uint64_t cached = 0xffffffff80000000ull + physical;
  const std::uint64_t address = region == 0   ? cached
                                : region == 1 ? cached + 0x20000000ull
                                              : 0x50000ull + index * 32;
  const unsigned conflict = physical + 0x4000;
  for (unsigned word = 0; word < 32; ++word) {
    p.physical_write(physical + word * 4, 0x4c201357u ^ word * 0x134219u ^ initial << 24);
    p.physical_write(conflict + word * 4, 0x87bcd321u ^ word * 0x345671u ^ operation << 20);
  }
  for (unsigned word = 0; word < 32; ++word)
    p.code(word * 4, 0);
  const std::uint64_t start = mode & 0x18 ? 0x40000ull : 0xffffffff80001000ull;
  const bool user_reverse = (mode & 0x18) == 0x10 && reverse;
  const unsigned lane = (route == 2 ? user_reverse : little ^ user_reverse) ? 4 : 0;
  const auto code = [&](unsigned offset, unsigned value) { p.code(offset ^ lane, value); };
  const unsigned before = delay ? 7 : 1;
  for (unsigned n = 0; n < before; ++n)
    code(n * 4, 0x24a50001);
  unsigned cursor = before;
  if (delay)
    code(cursor++ * 4, 0x08000000u | unsigned(((start + 0x200) >> 2) & 0x03ffffff));
  code(cursor++ * 4, instruction);
  code(cursor++ * 4, 26u << 21 | 8u);
  code(cursor++ * 4, 0);
  p.gpr(27, initial == 5 ? cached + 0x4000 : cached);
  if (initial == 1 || initial == 2 || initial == 4 || initial == 5 || initial == 7)
    p.execute(35u << 26 | 27u << 21 | 28u << 16);
  if (initial == 2 || initial == 4 || initial == 5 || initial == 7) {
    p.gpr(28, 0xffffffff9123abcdull);
    p.execute(43u << 26 | 27u << 21 | 28u << 16 | 4);
  }
  if (initial == 3 || initial == 4)
    p.execute(47u << 26 | 27u << 21 | 20u << 16);
  if (initial == 6) {
    p.control(28, ((physical >> 12) << 8) | 0xc0);
    p.execute(47u << 26 | 27u << 21 | 8u << 16);
    p.execute(47u << 26 | 27u << 21 | 9u << 16);
  }
  if (initial == 7)
    p.execute(47u << 26 | 27u << 21 | 17u << 16);
  p.gpr(1, address);
  p.gpr(26, start + 0x200);
  p.control(28, ((conflict >> 12) << 8) | ((initial & 3) << 6) | 0x35);
  p.control(29, 0xabcdef01);
  p.control(12, mode);
  p.control(16, little ? 0x70066460 : 0x7006e460);
  p.pc(start);
  for (auto value : {std::uint64_t(route), std::uint64_t(mode), address, std::uint64_t(little),
                     std::uint64_t(reverse), std::uint64_t(delay), std::uint64_t(operation),
                     std::uint64_t(initial), std::uint64_t(index)})
    p.emit(value);
  p.observe();
  if (route == 0) {
    if (delay)
      p.execute(0x10000002);
    p.execute(instruction);
  } else
    p.run(route == 2, start, start + cursor * 4);
  p.observe();
  for (unsigned word = 0; word < 32; ++word) {
    p.emit(p.physical_read(physical + word * 4));
    p.emit(p.physical_read(conflict + word * 4));
    p.emit(p.word(word * 4));
  }
  p.control(12, 0x30000080);
  p.control(16, 0x7006e460);
  p.pc(0xffffffffa0003000ull);
  for (unsigned tagop : {4u, 5u}) {
    const auto touched = tagop == 4 ? index : (index * 2) & 511;
    for (unsigned line : {0u, 1u, touched, touched + 1, 128u, 129u}) {
      p.gpr(27, 0xffffffff80000000ull + (line & 511) * (tagop == 4 ? 32 : 16));
      p.execute(47u << 26 | 27u << 21 | tagop << 16);
      p.emit(p.read_control(28));
      p.emit(p.read_control(29));
    }
  }
  p.gpr(27, cached);
  for (unsigned word = 0; word < 8; ++word) {
    p.execute(35u << 26 | 27u << 21 | 28u << 16 | word * 4);
    p.emit(p.reg(28));
  }
  p.observe();
  p.gpr(27, cached + 0x4000);
  p.execute(35u << 26 | 27u << 21 | 28u << 16);
  p.emit(p.reg(28));
  for (unsigned word = 0; word < 8; ++word) {
    p.emit(p.physical_read(physical + word * 4));
    p.emit(p.physical_read(conflict + word * 4));
  }
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p) {
  for (unsigned route = 0; route < 3; ++route)
    for (auto mode : modes)
      for (unsigned region = 0; region < 3; ++region)
        for (bool little : {false, true})
          for (bool reverse : {false, true})
            for (bool delay : {false, true})
              for (auto index : indices)
                for (unsigned initial = 0; initial < 8; ++initial)
                  for (unsigned operation = 0; operation < 32; ++operation)
                    run(p, route, mode, region, little, reverse, delay, operation, initial, index);
}
} // namespace test::cpu_replay::cache::maintenance
