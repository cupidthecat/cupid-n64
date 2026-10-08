#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

void fallback_timing() {
  for (unsigned kind = 0; kind < 7; ++kind) {
    for (unsigned before = 0; before < 8; ++before) {
      for (unsigned after = 0; after < 4; ++after) {
        for (bool delay : {false, true}) {
          for (bool timer : {false, true}) {
            native_memory::CachedFixture f;
            f.cpu.write_control(Status, 0x34000000);
            f.memory.words[0x200 / 4] = 0x1234;
            f.memory.put(0x200, 4, 0x1234);
            f.cpu.state().gpr[2] = kind == 0 ? 0xffffffffa0000200ull : 0xffffffff80000200ull;
            f.cpu.state().gpr[31] = 0xffffffff80003000;
            const auto instruction = kind < 4 ? i(35, 2, 4, 0) : 0x46031100u;
            if (kind == 2)
              f.cpu.execute(instruction);
            if (kind == 3)
              f.cpu.state().gpr[2] |= 1;
            f.cpu.state().fpr[2] = kind == 5 ? 0 : kind == 6 ? 0x7fbfffff : 0x3fc00000;
            f.cpu.state().fpr[3] = f.cpu.state().fpr[2];
            for (unsigned n = 0; n < before; ++n)
              f.code(n * 4, i(9, 3, 3, 1), false);
            unsigned word = before;
            if (delay) {
              f.code(word++ * 4, r(8, 31, 0, 0), false);
              f.code(word++ * 4, instruction, false);
            } else {
              f.code(word++ * 4, instruction, false);
              for (unsigned n = 0; n < after; ++n)
                f.code(word++ * 4, i(9, 3, 3, 1), false);
              f.code(word++ * 4, r(8, 31, 0, 0), false);
              f.code(word++ * 4, 0, false);
            }
            f.cpu.set_pc(0xffffffff80001000);
            f.cpu.write_control(Count, 0);
            f.cpu.write_control(Compare, 0xffffffff);
            const auto start = f.cpu.state().clocks;
            const bool fault = kind == 3 || kind == 6;
            const auto fetched = fault ? before + unsigned(delay) + 1 : word;
            unsigned clocks = 96 * ((fetched + 7) / 8) + before * 2;
            if (fault)
              clocks += kind == 3 ? 4 : delay ? 2 : 10;
            else if (delay)
              clocks += kind == 0 ? 2 : kind == 1 ? 82 : kind == 2 ? 6 : 12;
            else {
              clocks += after * 2 + (kind == 0 ? 8 : kind == 1 ? 88 : kind == 2 ? 8 : 14);
              if (kind < 2)
                clocks += before * 2;
            }
            if (timer)
              f.cpu.write_control(Compare, clocks / 4);
            equal(f.cpu.run_block(start + 10000), true);
            equal(f.cpu.state().clocks - start, clocks);
            equal(f.cpu.read_control(Count), clocks / 4);
            equal(f.cpu.state().gpr[3], before + (!fault && !delay ? after : 0));
            equal(f.cpu.state().gpr[4], kind < 3 ? 0x1234 : 0);
            equal(f.cpu.state().fpr[4], kind == 4 ? 0x40400000 : 0);
            equal(f.cpu.state().pc, fault ? 0xffffffff80000180ull : 0xffffffff80003000ull);
            const auto cause = fault ? (kind == 3 ? 0x10u : 0x3cu) | (delay ? 0x80000000u : 0u) : 0;
            equal(f.cpu.read_control(Cause), cause);
            f.cpu.synchronize_timer();
            equal(f.cpu.read_control(Cause), cause | (timer ? 0x8000u : 0u));
            equal(f.cpu.state().fcr31, kind == 6 ? 0x20000 : 0);
            if (fault)
              equal(f.cpu.read_control(Epc), 0xffffffff80001000ull + before * 4);
          }
        }
      }
    }
  }
}

} // namespace

void native_timing_tests() {
  fallback_timing();
}

} // namespace test
