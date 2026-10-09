#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
namespace test::cpu_replay::decode {
constexpr std::array<std::uint64_t, 16> values{0,
                                               1,
                                               2,
                                               31,
                                               32,
                                               63,
                                               0x7fff,
                                               0x8000,
                                               0x7fffffff,
                                               0x80000000,
                                               0xffffffff,
                                               0xffffffff80000000,
                                               0x7fffffffffffffff,
                                               0x8000000000000000,
                                               0xffffffffffffffff,
                                               0x0123456789abcdef};
constexpr std::array<unsigned, 12> modes{0,          0x10000000, 0x30000080, 0x30000010,
                                         0x30000030, 0x30000008, 0x30000048, 0x70000080,
                                         0x00000010, 0x00000008, 0x70000030, 0x70000048};
constexpr std::array immediates{2u,  3u,  4u,  5u,  6u,  7u,  8u,  9u,  10u, 11u,
                                12u, 13u, 14u, 15u, 20u, 21u, 22u, 23u, 24u, 25u};
template <class P> void prepare(P &p, unsigned mode, unsigned trial, unsigned instruction) {
  p.begin();
  std::uint64_t seed = 0x23b98ec64071da5full ^ (std::uint64_t(instruction) << 17) ^ trial;
  const auto next = [&] {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return seed ^ (seed >> 31);
  };
  for (unsigned reg = 0; reg < 32; ++reg) {
    p.gpr(reg, reg ? next() : 0);
    p.fpr(reg, next());
  }
  const auto hi = next(), lo = next();
  p.hilo(hi, lo);
  for (unsigned reg : {2u, 3u, 4u, 5u, 8u, 10u, 14u, 17u, 18u, 19u, 20u, 26u, 27u, 28u, 29u, 30u})
    p.control(reg, next());
  p.control(0, trial & 31);
  p.control(6, 31);
  p.control(9, 0);
  p.control(11, 0xffffffff);
  p.control(13, 0);
  p.control(12, mode);
  p.pc(0xffffffffa0001000ull);
  p.gpr(1, values[trial & 15]);
  p.gpr(2, values[(trial * 7 + 3) & 15]);
}
template <class P>
void run(P &p, unsigned mode, unsigned trial, unsigned instruction, bool delay,
         unsigned follow = 0) {
  prepare(p, mode, trial, instruction);
  p.emit(instruction);
  p.emit(delay ? 0x10000002u : 0);
  p.emit(follow);
  p.observe();
  if (delay)
    p.execute(0x10000002u);
  p.execute(instruction);
  p.observe();
  p.execute(follow);
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p) {
  for (auto mode : modes)
    for (bool delay : {false, true})
      for (unsigned function = 0; function < 64; ++function)
        for (unsigned trial = 0; trial < 16; ++trial)
          for (unsigned dest : {0u, 1u, 2u, 7u})
            run(p, mode, trial,
                1u << 21 | 2u << 16 | dest << 11 | ((trial * 7) & 31) << 6 | function, delay);
  for (auto mode : modes)
    for (bool delay : {false, true})
      for (unsigned condition = 0; condition < 32; ++condition)
        for (unsigned trial = 0; trial < 16; ++trial)
          run(p, mode, trial, 1u << 26 | 1u << 21 | condition << 16 | (values[trial] & 0xffff),
              delay);
  for (auto mode : modes)
    for (bool delay : {false, true})
      for (auto operation : immediates)
        for (unsigned trial = 0; trial < 16; ++trial)
          for (unsigned dest : {0u, 1u, 2u, 7u})
            run(p, mode, trial,
                operation << 26 | 1u << 21 | dest << 16 | (values[(trial + 5) & 15] & 0xffff),
                delay);
  for (auto mode : modes)
    for (bool delay : {false, true})
      for (unsigned transfer = 0; transfer < 32; ++transfer)
        for (unsigned control = 0; control < 32; ++control)
          for (unsigned trial = 0; trial < 4; ++trial)
            run(p, mode, trial * 5,
                16u << 26 | transfer << 21 | ((trial & 1) ? 0u : 1u) << 16 | control << 11 |
                    ((control + trial * 17) & 63),
                delay);
  for (auto mode : modes)
    for (bool delay : {false, true})
      for (unsigned transfer = 0; transfer < 32; ++transfer)
        for (unsigned reg : {0u, 1u, 7u, 31u})
          for (unsigned trial = 0; trial < 4; ++trial)
            run(p, mode, trial * 5, 18u << 26 | transfer << 21 | reg << 16 | trial << 11 | 0x345,
                delay, 0x48070000u);
}
} // namespace test::cpu_replay::decode
