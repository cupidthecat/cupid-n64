#pragma once
#include <array>
#include <cstdint>
namespace test::reset_reuse {
constexpr unsigned CpuCode = 0x7000;
constexpr unsigned Data = 0x7400;
inline std::array<std::uint32_t, 6> cpu_code(unsigned phase, unsigned variant) {
  const unsigned value = 0x1357 + phase * 0x210 + variant * 7;
  return {9u << 26 | 2u << 16 | value,
          43u << 26 | 1u << 21 | 2u << 16,
          9u << 26 | 2u << 21 | 3u << 16 | (phase + 5),
          8u | 31u << 21,
          9u << 26 | 4u << 16 | (phase + 9),
          0};
}
inline std::array<std::uint32_t, 4> rsp_code(unsigned phase, unsigned variant) {
  return {9u << 26 | 1u << 16 | (0x123 + phase * 0x100 + variant * 11),
          9u << 26 | 1u << 21 | 2u << 16 | (phase + 13), 43u << 26 | 2u << 16 | 0x100, 13};
}
template <class Probe> void observe(Probe &p) {
  p.word(p.pc());
  p.word(p.cpu_clock());
  for (unsigned reg = 0; reg < 32; ++reg)
    p.word(p.gpr(reg));
  for (unsigned reg : {9u, 11u, 12u, 13u, 16u, 30u})
    p.word(p.control(reg));
  p.word(p.raw_memory(Data));
  p.word(p.raw_memory(CpuCode));
  p.word(p.rsp_memory(0x100));
  p.word(p.rsp_memory(0x1000));
  p.internal();
}
template <class Probe> void scenarios(Probe &p) {
  for (bool expansion : {false, true})
    for (bool warm : {false, true})
      for (unsigned variant = 0; variant < 4; ++variant) {
        p.reset(expansion);
        test::machine_reset::initialize(p);
        for (unsigned phase = 0; phase < 3; ++phase) {
          if (phase) {
            p.power(warm);
            if (!warm)
              test::machine_reset::initialize(p);
          }
          p.install(phase, variant);
          p.execute_cpu();
          p.execute_rsp();
          test::reset_reuse::observe(p);
        }
        p.finish();
      }
}
} // namespace test::reset_reuse
