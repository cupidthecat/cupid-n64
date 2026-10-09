#pragma once
#include <array>
#include <cstdint>
namespace test::rsp_replay::arithmetic {
constexpr std::uint32_t opcode(unsigned op, unsigned dest, unsigned source, unsigned target,
                               unsigned element) {
  return 0x4a000000u | element << 21 | target << 16 | source << 11 | dest << 6 | op;
}
template <class P> void snapshot(P &p) {
  p.registers();
}
template <class P> void scenarios(P &p) {
  constexpr std::array<unsigned, 10> values{0,      1,      0x7ffe, 0x7fff, 0x8000,
                                            0x8001, 0xfffe, 0xffff, 0x5555, 0xaaaa};
  constexpr std::array<std::uint64_t, 12> accumulators{
      0,          1,          0x7fff,         0x8000,         0xffff,         0x7fffffff,
      0x80000000, 0xffffffff, 0x7fffffffffff, 0x800000000000, 0xffffffffffff, 0xffff80000000};
  for (unsigned mode = 0; mode < 3; ++mode)
    for (unsigned operation = 0; operation < 64; ++operation)
      for (unsigned element = 0; element < 16; ++element)
        for (unsigned trial = 0; trial < 16; ++trial) {
          p.mode(mode);
          std::uint64_t seed =
              0x93271fe1ba564c87ull ^ std::uint64_t(operation << 12 | element << 8 | trial);
          const auto next = [&] {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            return seed ^ (seed >> 29);
          };
          for (unsigned reg = 0; reg < 32; ++reg) {
            p.set_reg(reg, reg ? static_cast<unsigned>(next()) : 0);
            for (unsigned lane = 0; lane < 8; ++lane)
              p.set_vector(reg, lane,
                           trial < 10 ? values[(trial + reg * 3 + lane) % values.size()] : next());
          }
          for (unsigned lane = 0; lane < 8; ++lane)
            p.set_accumulator(lane, trial < 12 ? accumulators[(trial + lane) % accumulators.size()]
                                               : next());
          const auto flags = next();
          const auto input = next();
          const auto output = next();
          p.set_flags(flags, input, output, trial & 1);
          const auto source = (trial * 7) & 31;
          const auto target = trial % 4 == 3 ? source : (source + 11) & 31;
          const auto dest = trial % 4 == 1 ? source : trial % 4 == 2 ? target : (source + 19) & 31;
          p.execute(opcode(operation, dest, source, target, element));
          snapshot(p);
          p.execute(opcode(0x1d, (dest + 1) & 31, source, target, 8 + trial % 3));
          p.execute(opcode(0x24, dest, source, target, element));
          p.execute(opcode(0x27, (dest + 2) & 31, dest, source, element));
          if (operation >= 0x30 && operation <= 0x36) {
            p.execute(opcode(0x32, dest, source, target, element));
            p.execute(opcode(0x31, target, source, dest, element));
            p.execute(opcode(0x36, source, target, dest, element));
            p.execute(opcode(0x35, dest, target, source, element));
          }
          snapshot(p);
          p.finish();
        }
}
} // namespace test::rsp_replay::arithmetic
