#pragma once
#include <cstdint>
namespace test::rsp_replay::memory {
template <class P>
void prepare(P &p, unsigned operation, unsigned element, unsigned trial, unsigned address) {
  std::uint64_t seed =
      0x93fa217c508d6be4ull ^ std::uint64_t(operation << 12 | element << 8 | trial);
  const auto next = [&] {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return seed ^ (seed >> 29);
  };
  for (unsigned reg = 0; reg < 32; ++reg) {
    p.set_reg(reg, reg ? static_cast<unsigned>(next()) : 0);
    for (unsigned lane = 0; lane < 8; ++lane)
      p.set_vector(reg, lane, next());
  }
  for (unsigned lane = 0; lane < 8; ++lane)
    p.set_accumulator(lane, next());
  const auto flags = next();
  const auto input = next();
  const auto output = next();
  p.set_flags(flags, input, output, trial & 1);
  for (unsigned local = 0; local < 4096; local += 8)
    p.local_write(local, next());
  p.set_reg(13, address);
}
template <class P>
void run_case(P &p, bool store, unsigned operation, unsigned element, unsigned trial,
              unsigned address, unsigned immediate) {
  prepare(p, operation, element, trial, address);
  const auto target = (element * 3 + address * 7 + trial * 11) & 31;
  const auto instruction = (store ? 58u : 50u) << 26 | 13u << 21 | target << 16 | operation << 11 |
                           element << 7 | (immediate & 127);
  p.execute(instruction);
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p) {
  p.mode(2);
  for (bool store : {false, true})
    for (unsigned operation = 0; operation < 32; ++operation)
      for (unsigned element = 0; element < 16; ++element)
        for (unsigned offset = 0; offset < 16; ++offset)
          for (unsigned trial = 0; trial < 2; ++trial)
            run_case(p, store, operation, element, trial, (trial ? 0xfffffff0u : 0xff0u) + offset,
                     0);
  for (bool store : {false, true})
    for (unsigned operation = 0; operation < 12; ++operation)
      for (unsigned immediate = 0; immediate < 128; ++immediate)
        for (unsigned trial = 0; trial < 2; ++trial)
          run_case(p, store, operation, immediate & 15, trial, trial ? 0x1003u : 0xffdu, immediate);
}
} // namespace test::rsp_replay::memory
