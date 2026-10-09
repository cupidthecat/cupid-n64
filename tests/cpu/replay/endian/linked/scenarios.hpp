#pragma once
#include "../scenarios.hpp"
namespace test::cpu_replay::endian::linked {
template <class P>
void instruction(P &p, unsigned route, unsigned mode, bool delay, unsigned opcode) {
  const std::uint64_t start = mode & 0x18 ? 0x40000ull : 0xffffffff80001000ull;
  for (unsigned n = 0; n < 32; ++n)
    p.code(n * 4, 0);
  // Faults, Status writes, and ERET can change whether user RE applies.
  const auto status = p.read_control(12);
  const auto config = p.read_control(16);
  const bool user = (status & 6) == 0 && (status & 0x18) == 0x10;
  const bool reversed = user && (status & 0x02000000);
  const bool little = (config & 0x8000) == 0;
  const unsigned lane = (route == 2 ? reversed : little ^ reversed) ? 4u : 0u;
  const auto code = [&](unsigned offset, unsigned value) { p.code(offset ^ lane, value); };
  p.emit(status);
  p.emit(config);
  p.emit(lane);
  const auto before = delay ? 7u : 1u;
  for (unsigned n = 0; n < before; ++n)
    code(n * 4, 0x24a50001u);
  unsigned word = before;
  if (delay)
    code(word++ * 4, 0x08000000u | unsigned(((start + 0x200) >> 2) & 0x03ffffff));
  code(word++ * 4, opcode);
  code(word++ * 4, 26u << 21 | 8u);
  code(word++ * 4, 0);
  for (unsigned offset = 0; offset < 128; offset += 32) {
    p.gpr(25, start + offset);
    p.execute(47u << 26 | 25u << 21);
  }
  p.gpr(26, start + 0x200);
  p.pc(start);
  p.emit(opcode);
  for (unsigned n = 0; n < 32; ++n)
    p.emit(p.word(n * 4));
  p.run(route == 2, start, start + word * 4);
  p.observe();
  for (unsigned n = 0; n < 32; ++n)
    p.emit(p.physical_read(0x2000 + n * 4));
}
template <class P>
void run(P &p, unsigned route, unsigned mode, unsigned region, bool little, bool reverse, bool warm,
         bool wide, unsigned offset, unsigned history, bool delay) {
  mode |= reverse ? 0x02000000u : 0;
  const auto load = (wide ? 52u : 48u) << 26 | 1u << 21 | 2u << 16 | offset;
  decode::prepare(p, mode, (history + offset) & 15, load);
  p.mapping(mode);
  p.control(12, 0x10000000);
  p.control(0, 2);
  p.control(2, (0x2000 >> 6) | 0x1f);
  p.control(3, (0x3000 >> 6) | 0x1f);
  p.control(5, 0);
  p.control(10, 0x50000);
  p.execute(0x42000002);
  p.control(12, mode);
  p.control(16, little ? 0x70066460u : 0x7006e460u);
  for (unsigned word = 0; word < 32; ++word)
    p.physical_write(0x2000 + word * 4, 0x10203040u ^ (word * 0x172345u));
  if (warm) {
    p.control(12, 0x30000080);
    p.gpr(27, 0xffffffff80002000ull);
    p.execute(35u << 26 | 27u << 21 | 28u << 16);
    p.control(12, mode);
  }
  const auto address = endian::addresses[region];
  p.gpr(1, address);
  p.gpr(2, 0x89abcdef01234567ull);
  for (auto value :
       {std::uint64_t(route), std::uint64_t(mode), std::uint64_t(region), std::uint64_t(little),
        std::uint64_t(reverse), std::uint64_t(warm), std::uint64_t(wide), std::uint64_t(offset),
        std::uint64_t(history), std::uint64_t(delay)})
    p.emit(value);
  p.observe();
  for (unsigned n = 0; n < 32; ++n)
    p.emit(p.physical_read(0x2000 + n * 4));
  instruction(p, route, mode, delay, load);
  p.gpr(3, address);
  p.gpr(4, 0x88776655ccaabb99ull);
  unsigned middle = 0;
  switch (history) {
  case 1:
    middle = 43u << 26 | 3u << 21 | 4u << 16;
    break;
  case 2:
    middle = 43u << 26 | 3u << 21 | 4u << 16 | 8u;
    break;
  case 3:
    middle = 43u << 26 | 3u << 21 | 4u << 16 | 64u;
    break;
  case 4:
    middle = (wide ? 60u : 56u) << 26 | 3u << 21 | 4u << 16 | offset;
    break;
  case 5:
    middle = 48u << 26 | 3u << 21 | 4u << 16 | 16u;
    break;
  case 6:
    middle = 52u << 26 | 3u << 21 | 4u << 16 | 16u;
    break;
  case 7:
    middle = 35u << 26 | 3u << 21 | 4u << 16 | 1u;
    break;
  case 8:
    middle = 13;
    break;
  case 9:
    p.control(12, mode | 2);
    p.control(14, 0x40000200);
    middle = 0x42000018;
    break;
  case 10:
    p.gpr(3, 0x60000);
    middle = 35u << 26 | 3u << 21 | 4u << 16;
    break;
  case 11:
    middle = 47u << 26 | 3u << 21 | 21u << 16;
    break;
  case 12:
    p.gpr(4, mode | 2);
    middle = 16u << 26 | 4u << 21 | 4u << 16 | 12u << 11;
    break;
  case 13:
    middle = (wide ? 52u : 48u) << 26 | 3u << 21 | 4u << 16 | 1u;
    break;
  }
  instruction(p, route, mode, delay, middle);
  p.gpr(1, address);
  p.gpr(2, 0x89abcdef01234567ull);
  instruction(p, route, mode, delay, (wide ? 60u : 56u) << 26 | 1u << 21 | 2u << 16 | offset);
  p.control(12, 0x30000080);
  p.control(16, 0x7006e460);
  p.pc(0xffffffffa0003000ull);
  for (unsigned operation : {4u, 5u})
    for (unsigned index = 0; index < 8; ++index) {
      const auto tag_index = operation == 4 && index >= 4 ? 128 + index - 4 : index;
      p.gpr(27, 0xffffffff80000000ull + tag_index * (operation == 4 ? 32 : 16));
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
  p.gpr(27, 0xffffffff80002000ull);
  for (unsigned offset = 0; offset < 128; offset += 8) {
    p.execute(55u << 26 | 27u << 21 | 28u << 16 | offset);
    p.emit(p.reg(28));
  }
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p) {
  for (unsigned route = 1; route < 3; ++route)
    for (auto mode : endian::modes)
      for (unsigned region = 0; region < 3; ++region)
        for (bool little : {false, true})
          for (bool reverse : {false, true})
            for (bool warm : {false, true})
              for (bool wide : {false, true})
                for (unsigned offset = 0; offset < 8; ++offset)
                  for (unsigned history = 0; history < 14; ++history)
                    for (bool delay : {false, true})
                      run(p, route, mode, region, little, reverse, warm, wide, offset, history,
                          delay);
}
} // namespace test::cpu_replay::endian::linked
