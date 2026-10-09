#pragma once
#include "../decode/scenarios.hpp"
namespace test::cpu_replay::cached {
template <class P>
void run(P &p, bool native, unsigned mode, unsigned trial, unsigned instruction, bool delay) {
  decode::prepare(p, mode, trial, instruction);
  p.mapping(mode);
  const std::uint64_t start = mode & 0x18 ? 0x40000ull : 0xffffffff80001000ull;
  const auto count = delay ? 7u : 1u;
  p.gpr(26, start + 0x200);
  for (unsigned offset = 0; offset < 128; offset += 4)
    p.code(offset, 0);
  for (unsigned n = 0; n < count; ++n)
    p.code(n * 4, 0x24a50001u);
  unsigned word = count;
  if (delay)
    p.code(word++ * 4, 0x08000000u | unsigned(((start + 0x200) >> 2) & 0x03ffffff));
  p.code(word++ * 4, instruction);
  p.code(word++ * 4, 26u << 21 | 8u);
  p.code(word++ * 4, 0);
  p.pc(start);
  p.emit(native);
  p.emit(instruction);
  p.emit(delay);
  p.observe();
  p.run(native, start, start + word * 4);
  p.observe();
  for (unsigned offset = 0; offset < 128; offset += 4)
    p.emit(p.word(offset));
  p.control(12, 0x10000000);
  p.pc(0xffffffffa0003000ull);
  for (unsigned operation : {4u, 5u})
    for (unsigned index = 0; index < 512; ++index) {
      p.gpr(27, 0xffffffff80000000ull + index * (operation == 4 ? 32 : 16));
      p.execute(47u << 26 | 27u << 21 | operation << 16);
      p.emit(p.read_control(28));
      p.emit(p.read_control(29));
    }
  for (unsigned index = 0; index < 32; ++index) {
    p.control(0, index);
    p.execute(0x42000001u);
    for (unsigned reg : {2u, 3u, 5u, 10u})
      p.emit(p.read_control(reg));
  }
  p.finish();
}
template <class P> void scenarios(P &p) {
  constexpr std::array modes{0x10000000u, 0x30000080u, 0x30000010u, 0x30000030u,
                             0x00000010u, 0x00000008u, 0x70000030u, 0x70000048u};
  for (bool native : {false, true})
    for (auto mode : modes)
      for (bool delay : {false, true}) {
        for (unsigned function = 0; function < 64; ++function)
          for (unsigned trial : {0u, 1u, 8u, 9u, 12u, 13u, 14u, 15u})
            for (unsigned dest : {0u, 7u})
              run(p, native, mode, trial,
                  1u << 21 | 2u << 16 | dest << 11 | ((trial * 7) & 31) << 6 | function, delay);
        for (unsigned condition = 0; condition < 32; ++condition)
          for (unsigned trial : {0u, 8u, 13u, 14u})
            run(p, native, mode, trial, 1u << 26 | 1u << 21 | condition << 16 | 3u, delay);
        for (unsigned operation : decode::immediates)
          for (unsigned trial : {0u, 8u, 13u, 14u})
            for (unsigned dest : {0u, 7u})
              run(p, native, mode, trial, operation << 26 | 1u << 21 | dest << 16 | 0x8000u, delay);
        for (unsigned transfer = 0; transfer < 32; ++transfer)
          for (unsigned control = 0; control < 32; ++control)
            for (unsigned trial : {0u, 14u})
              run(p, native, mode, trial,
                  16u << 26 | transfer << 21 | 1u << 16 | control << 11 | ((control + trial) & 63),
                  delay);
        for (unsigned transfer = 0; transfer < 32; ++transfer)
          for (unsigned reg : {0u, 1u, 7u, 31u})
            for (unsigned trial : {0u, 14u})
              run(p, native, mode, trial,
                  18u << 26 | transfer << 21 | reg << 16 | trial << 11 | 0x345u, delay);
      }
}
} // namespace test::cpu_replay::cached
