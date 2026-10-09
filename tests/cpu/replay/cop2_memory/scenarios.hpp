#pragma once
#include "../endian/scenarios.hpp"
namespace test::cpu_replay::cop2_memory {
template <class P>
void run(P &p, unsigned route, unsigned mode, bool little, bool reverse, bool enabled, bool delay,
         unsigned operation, unsigned selector, unsigned target) {
  mode |= (reverse ? 0x02000000u : 0) | (enabled ? 0x40000000u : 0);
  const auto instruction = operation << 26 | selector << 21 | target << 16 | 4u;
  decode::prepare(p, mode, selector & 15, instruction);
  p.mapping(mode);
  p.control(12, mode | 0x40000000);
  p.gpr(7, 0x8123456789abcdefull);
  p.execute(0x12u << 26 | 5u << 21 | 7u << 16);
  p.control(12, mode);
  p.control(16, little ? 0x70066460u : 0x7006e460u);
  if (target)
    p.gpr(target, 0xfedcba9876543210ull);
  for (unsigned word = 0; word < 32; ++word)
    p.physical_write(0x2000 + word * 4, 0x10203040u ^ (word * 0x172345u));
  const std::uint64_t start = mode & 0x18 ? 0x40000ull : 0xffffffff80001000ull;
  p.gpr(26, start + 0x200);
  for (unsigned word = 0; word < 32; ++word)
    p.code(word * 4, 0);
  const bool reversed = (mode & 0x18) == 0x10 && reverse;
  const unsigned lane = (route == 2 ? reversed : little ^ reversed) ? 4u : 0u;
  const auto code = [&](unsigned offset, unsigned value) { p.code(offset ^ lane, value); };
  const auto jump = 0x08000000u | unsigned(((start + 0x200) >> 2) & 0x03ffffff);
  unsigned word = 0;
  if (delay)
    code(word++ * 4, jump);
  code(word++ * 4, instruction);
  code(word++ * 4, 26u << 21 | 8u);
  code(word++ * 4, 0);
  p.pc(start);
  for (auto value : {std::uint64_t(route), std::uint64_t(instruction), std::uint64_t(mode),
                     std::uint64_t(little), std::uint64_t(reverse), std::uint64_t(enabled),
                     std::uint64_t(delay), std::uint64_t(selector), std::uint64_t(target)})
    p.emit(value);
  p.observe();
  for (unsigned n = 0; n < 32; ++n)
    p.emit(p.word(n * 4));
  for (unsigned n = 0; n < 32; ++n)
    p.emit(p.physical_read(0x2000 + n * 4));
  if (route == 0) {
    if (delay)
      p.execute(jump);
    p.execute(instruction);
  } else if (route == 1) {
    if (delay)
      p.single(false);
    p.single(false);
  } else
    p.run(true, start, start + word * 4);
  p.observe();
  for (unsigned n = 0; n < 32; ++n)
    p.emit(p.physical_read(0x2000 + n * 4));
  p.control(12, 0x70000080);
  p.control(16, 0x7006e460);
  p.pc(0xffffffffa0003000ull);
  p.execute(0x12u << 26 | 1u << 21 | 8u << 16);
  p.emit(p.reg(8));
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p) {
  for (unsigned route = 0; route < 3; ++route)
    for (auto mode : endian::modes)
      for (bool little : {false, true})
        for (bool reverse : {false, true})
          for (bool enabled : {false, true})
            for (bool delay : {false, true})
              for (unsigned operation : {50u, 54u, 58u, 62u})
                for (unsigned selector = 0; selector < 32; ++selector)
                  for (unsigned target : {0u, 2u, 7u, 28u, 29u, 31u})
                    run(p, route, mode, little, reverse, enabled, delay, operation, selector,
                        target);
}
} // namespace test::cpu_replay::cop2_memory
