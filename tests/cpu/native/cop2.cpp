#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;

void native_cop2_noop_tests() {
  for (unsigned format : {16u, 31u}) {
    for (unsigned route = 0; route < 3; ++route) {
      for (bool enabled : {false, true}) {
        for (bool delay : {false, true}) {
          for (unsigned before : {0u, 7u}) {
            native_memory::CachedFixture f;
            const unsigned status = enabled ? 0x50000000u : 0x10000000u;
            f.cpu.write_control(Status, status);
            f.cpu.write_control(Epc, 0x12345678);
            f.cpu.write_control(Count, 0);
            f.cpu.write_control(Compare, 0xffffffff);
            for (unsigned reg = 1; reg < 32; ++reg)
              f.cpu.state().gpr[reg] = 0x123456789abcdef0ull ^ reg;
            f.cpu.state().gpr[5] = 0;
            f.cpu.state().gpr[26] = 0xffffffff80003000ull;
            auto expected = f.cpu.state().gpr;
            expected[5] = before;
            const auto instruction = 18u << 26 | format << 21 | 0x345u;
            for (unsigned word = 0; word < before; ++word)
              f.code(word * 4, i(9, 5, 5, 1));
            unsigned word = before;
            if (delay)
              f.code(word++ * 4, r(8, 26, 0, 0));
            f.code(word++ * 4, instruction);
            if (!delay) {
              f.code(word++ * 4, r(8, 26, 0, 0));
              f.code(word++ * 4, 0);
            }
            const auto start = f.cpu.state().clocks;
            if (route == 0)
              equal(f.cpu.run_block(start + 1), true);
            else if (route == 1)
              equal(f.cpu.run_interpreted_block(start + 1), true);
            else {
              word = before + (delay ? 2 : 1);
              for (unsigned n = 0; n < word; ++n)
                f.cpu.step();
            }
            for (unsigned reg = 0; reg < 32; ++reg)
              equal(f.cpu.state().gpr[reg], expected[reg]);
            equal(f.cpu.state().clocks - start, 96 * ((word + 7) / 8) + 2 * word);
            equal(f.cpu.in_delay_slot(), false);
            if (route < 2) {
              equal(f.cpu.state().pc, 0xffffffff80003000ull);
              equal(f.cpu.read_control(Status), status);
              equal(f.cpu.read_control(Cause), 0);
              equal(f.cpu.read_control(Epc), 0x12345678);
            } else {
              equal(f.cpu.state().pc, 0xffffffff80000180ull);
              equal(f.cpu.read_control(Status), status | 2);
              equal(f.cpu.read_control(Cause),
                    (delay ? 0x80000000u : 0) | 0x20000000u | (enabled ? 40u : 44u));
              equal(f.cpu.read_control(Epc), 0xffffffff80001000ull + before * 4);
            }
          }
        }
      }
    }
  }
}
} // namespace test
