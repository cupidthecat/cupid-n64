#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

void batches() {
  for (unsigned format : {2u, 6u, 8u}) {
    for (unsigned target : {0u, 2u, 6u, 8u, 28u, 31u}) {
      for (unsigned control : {0u, 9u, 12u, 31u}) {
        for (bool unused_bits : {false, true}) {
          for (bool enabled : {false, true}) {
            const auto instruction = c(format, target, control) | (unused_bits ? 0x7ffu : 0);
            for (unsigned before = 0; before < 8; ++before) {
              for (unsigned flow = 0; flow < 5; ++flow) {
                const bool delay = flow == 1;
                const bool memory = flow >= 2;
                native_memory::CachedFixture f;
                const auto status = enabled ? 0x34000000u : 0x04000000u;
                f.cpu.write_control(Status, status);
                for (unsigned reg = 1; reg < 32; ++reg)
                  f.cpu.state().gpr[reg] = 0x123456789abcdef0ull ^ reg;
                f.cpu.state().gpr[2] = flow == 2 ? 0xffffffffa0000200ull : 0xffffffff80000200ull;
                f.memory.words[0x200 / 4] = 0x1234;
                f.memory.put(0x200, 4, 0x1234);
                if (flow == 4)
                  f.cpu.execute(i(35, 2, 8, 0));
                f.cpu.state().gpr[6] = 0;
                f.cpu.state().gpr[8] = 0xabcdef;
                f.cpu.state().gpr[31] = 0xffffffff80003000;
                auto expected = f.cpu.state().gpr;
                expected[6] = before;
                if (memory)
                  expected[8] = 0x1234;
                for (unsigned word = 0; word < before; ++word)
                  f.code(word * 4, i(9, 6, 6, 1));
                unsigned word = before;
                if (delay) {
                  f.code(word++ * 4, r(8, 31, 0, 0));
                  f.code(word++ * 4, instruction);
                } else {
                  f.code(word++ * 4, instruction);
                  if (memory)
                    f.code(word++ * 4, i(35, 2, 8, 0));
                  f.code(word++ * 4, r(8, 31, 0, 0));
                  f.code(word++ * 4, 0);
                }
                f.cpu.set_pc(0xffffffff80001000);
                f.cpu.write_control(Count, 0);
                f.cpu.write_control(Compare, 0xffffffff);
                const auto start = f.cpu.state().clocks;
                unsigned clocks = 96 * ((word + 7) / 8) + before * 2;
                if (memory) {
                  clocks += flow == 3 ? 90 : 10;
                  if (flow != 4 && before < 7)
                    clocks += before * 2 + 2;
                } else
                  clocks += delay ? 4 : 6;
                equal(f.cpu.run_block(start + 10000), true);
                equal(f.cpu.state().clocks - start, clocks);
                equal(f.cpu.read_control(Count), clocks / 4);
                for (unsigned reg = 0; reg < 32; ++reg)
                  equal(f.cpu.state().gpr[reg], expected[reg]);
                equal(f.cpu.state().pc, 0xffffffff80003000);
                equal(f.cpu.in_delay_slot(), false);
                equal(f.cpu.read_control(Status), status);
                equal(f.cpu.read_control(Cause), 0);
                equal(f.cpu.read_control(Epc), 0);
                equal(f.cpu.read_control(Compare), 0xffffffff);
              }
            }
          }
        }
      }
    }
  }
}

} // namespace

void native_control_noop_timing_tests() {
  batches();
}

} // namespace test
