#pragma once

#include "../../support/test.hpp"
#include "host/rounding.hpp"
#include <bit>
#include <memory>

namespace test {

inline void integer_rounding_tests() {
  using namespace cupid::n64;
  constexpr std::array input{0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, -3.5, 4.25, -4.25, 8.0, -8.0};
  constexpr std::array<std::int64_t, 12> even{0, 2, 2, 4, 0, -2, -2, -4, 4, -4, 8, -8};
  constexpr std::array<std::int64_t, 12> away{1, 2, 3, 4, -1, -2, -3, -4, 4, -4, 8, -8};
  constexpr std::array<std::int64_t, 12> trunc{0, 1, 2, 3, 0, -1, -2, -3, 4, -4, 8, -8};
  constexpr std::array<std::int64_t, 12> ceil{1, 2, 3, 4, 0, -1, -2, -3, 5, -4, 8, -8};
  constexpr std::array<std::int64_t, 12> floor{0, 1, 2, 3, -1, -2, -3, -4, 4, -5, 8, -8};
  for (bool single : {false, true})
    for (bool wide : {false, true})
      for (unsigned operation = 0; operation < 5; ++operation)
        for (bool enabled : {false, true})
          for (unsigned n = 0; n < input.size(); ++n) {
            auto fixture = std::make_unique<Fixture>();
            auto &cpu = fixture->cpu;
            cpu.write_control(Status, 0x34000000);
            cpu.state().fcr31 = enabled ? 128 : 0;
            cpu.set_pc(0xffffffff80001000);
            cpu.state().fpr[2] = single ? std::bit_cast<std::uint32_t>(float(input[n]))
                                        : std::bit_cast<std::uint64_t>(input[n]);
            cpu.state().fpr[4] = 0x123456789abcdef0;
            const auto function = operation < 4 ? (wide ? 8u : 12u) + operation
                                  : wide        ? 0x25u
                                                : 0x24u;
            const auto opcode =
                17u << 26 | (single ? 16u : 17u) << 21 | 2u << 11 | 4u << 6 | function;
            const auto before = cpu.state().clocks;
            cpu.execute(opcode);
            const bool inexact = n < 10 && (operation < 2 || operation == 4 ||
                                            cpu_fpu::conversion_inexact(single, wide));
            const bool trapped = enabled && inexact;
            const auto result = operation == 0
                                    ? (floating::nearest_integer_even ? even[n] : away[n])
                                : operation == 1 ? trunc[n]
                                : operation == 2 ? ceil[n]
                                : operation == 3 ? floor[n]
                                                 : even[n];
            equal(cpu.state().fpr[4], trapped ? 0x123456789abcdef0
                                      : wide  ? static_cast<std::uint64_t>(result)
                                              : static_cast<std::uint32_t>(result));
            equal(cpu.state().fcr31,
                  (enabled ? 128 : 0) | (inexact ? 0x1000 : 0) | (inexact && !enabled ? 4 : 0));
            equal((cpu.read_control(Cause) >> 2) & 31, trapped ? 15 : 0);
            equal(cpu.state().clocks - before, trapped ? 2 : 10);
            equal(cpu.state().pc, trapped ? 0xffffffff80000180 : 0xffffffff80001004);
          }
}

} // namespace test
