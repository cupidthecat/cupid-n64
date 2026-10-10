#pragma once
#include "values.hpp"
#include <initializer_list>
#include <span>
namespace test::cpu_replay::fpu::numeric {
template <class P>
void run(P &p, unsigned route, unsigned format, unsigned operation, unsigned csr, std::uint64_t a,
         std::uint64_t b, unsigned layout = 0, unsigned mode = 0x34000000, bool delay = false) {
  p.begin();
  for (unsigned reg = 0; reg < 32; ++reg) {
    p.gpr(reg, reg ? 0xfedcba9876543210ull + reg : 0);
    p.fpr(reg, 0x1020304050607080ull * (reg + 1) ^ 0x1723456789abcdefull);
  }
  p.hilo(0x123456789abcdef0ull, 0xfedcba9876543210ull);
  p.control(9, 0);
  p.control(11, 0xffffffff);
  p.control(13, 0);
  p.control(12, mode);
  p.control(16, 0x7006e460);
  const unsigned source = layout == 1 ? 3 : layout == 2 ? 31 : 2;
  const unsigned target = layout == 2 ? 1 : 4;
  const unsigned dest = layout == 3 ? source : layout == 4 ? target : layout == 5 ? 31 : 6;
  p.fpr((mode & 0x04000000) ? source : source & ~1u, a);
  p.fpr(target, b);
  const unsigned control = csr | 0x00800000 | ((a & 1) ? 0x7c : 0);
  p.fpu_control(control);
  const unsigned instruction =
      0x44000000u | format << 21 | target << 16 | source << 11 | dest << 6 | operation;
  const std::uint64_t start = route ? 0xffffffff80001000ull : 0xffffffffa0001000ull;
  p.pc(start);
  p.gpr(26, start + 0x100);
  if (delay) {
    p.code(0, 0x1000003fu);
    p.code(4, instruction);
    p.code(8, 0);
  } else {
    p.code(0, instruction);
    p.code(4, 26u << 21 | 8u);
    p.code(8, 0);
  }
  for (auto value :
       {std::uint64_t(route), std::uint64_t(mode), std::uint64_t(format), std::uint64_t(operation),
        std::uint64_t(control), a, b, std::uint64_t(layout), std::uint64_t(delay)})
    p.emit(value);
  p.observe();
  if (!route) {
    if (delay)
      p.execute(0x1000003fu);
    p.execute(instruction);
  } else
    p.run(route == 2, start, start + 12);
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p, unsigned routes) {
  for (unsigned route = 0; route < routes; ++route) {
    for (unsigned format : {16u, 17u}) {
      const auto &values = format == 16 ? single : dual;
      for (unsigned index = 0; index < controls.size(); ++index) {
        const unsigned control = controls[index];
        for (auto operation : operations) {
          for (unsigned sample = 0; sample < values.size(); ++sample) {
            const auto a = values[sample];
            const unsigned pairs = operation < 4 ? 4u : 1u;
            for (unsigned pair = 0; pair < pairs; ++pair) {
              const auto b = operation < 4 ? values[std::array{0u, 9u, 15u, 22u}[pair]]
                                           : values[(sample * 7 + 3) % values.size()];
              run(p, route, format, operation, control, a, b);
            }
          }
        }
      }
    }
    for (unsigned format : {20u, 21u})
      for (unsigned operation : {0x20u, 0x21u})
        for (unsigned index = 0; index < controls.size(); ++index)
          for (auto value : integers)
            run(p, route, format, operation, controls[index], value, 0);
    for (unsigned mode : {0x30000000u, 0x34000000u, 0x10000000u, 0x14000000u})
      for (bool delay : {false, true})
        for (unsigned layout = 0; layout < 6; ++layout)
          for (unsigned format : {16u, 17u})
            for (unsigned operation : {0u, 3u, 6u, 0x24u, 0x32u})
              for (unsigned index = 0; index < 4; ++index) {
                const auto &values = format == 16 ? single : dual;
                run(p, route, format, operation, controls[index], values[9 + index],
                    values[index == 2 ? 26 : 15], layout, mode, delay);
              }
    {
      std::uint64_t seed = 0xd9a34270cf81b65eull;
      const auto next = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
      };
      for (unsigned format : {16u, 17u, 20u, 21u}) {
        const auto functions = format < 20 ? std::span<const unsigned>(operations)
                                           : std::span<const unsigned>(operations).subspan(16, 2);
        for (auto operation : functions)
          for (unsigned sample = 0; sample < 64; ++sample) {
            const auto a = next(), b = next();
            const auto csr = static_cast<unsigned>(next()) & 0x01800f83;
            run(p, route, format, operation, csr, a, b, sample % 6,
                0x30000000 | ((sample & 1) << 26), sample & 2);
          }
      }
    }
  }
}
} // namespace test::cpu_replay::fpu::numeric
