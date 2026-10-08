#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr std::uint32_t operations[] = {0x00850018, 0x00850019, 0x0085001a, 0x0085001b, 0x0085001c,
                                        0x0085001d, 0x0085001e, 0x0085001f, 0x00853820, 0x00853822,
                                        0x0085382c, 0x0085382e, 0x20870001, 0x60870001};
constexpr unsigned latencies[] = {8, 8, 72, 72, 14, 14, 136, 136, 0, 0, 0, 0, 0, 0};

void batches() {
  for (unsigned kind = 0; kind < 14; ++kind) {
    for (unsigned before = 0; before < 8; ++before) {
      for (unsigned access = 0; access < 3; ++access) {
        for (bool zero : {false, true}) {
          native_memory::CachedFixture f;
          f.cpu.write_control(Status, 0x34000000);
          f.cpu.state().gpr[2] = access == 0 ? 0xffffffffa0000200ull : 0xffffffff80000200ull;
          f.memory.words[0x200 / 4] = 0x1234;
          f.memory.put(0x200, 4, 0x1234);
          if (access == 2)
            f.cpu.execute(i(35, 2, 8, 0));
          f.cpu.state().gpr[4] = 7;
          f.cpu.state().gpr[5] = zero ? 0 : 3;
          f.cpu.state().gpr[31] = 0xffffffff80003000;
          for (unsigned word = 0; word < before; ++word)
            f.code(word * 4, i(9, 6, 6, 1), false);
          f.code(before * 4, operations[kind], false);
          f.code((before + 1) * 4, i(35, 2, 8, 0), false);
          f.code((before + 2) * 4, r(8, 31, 0, 0), false);
          f.code((before + 3) * 4, 0, false);
          f.cpu.set_pc(0xffffffff80001000);
          f.cpu.write_control(Count, 0);
          f.cpu.write_control(Compare, 0xffffffff);
          const auto start = f.cpu.state().clocks;
          const auto batch = before * 2 + latencies[kind] + 2;
          unsigned clocks = 96 * ((before + 11) / 8) + batch + (access == 1 ? 88 : 8);
          if (access < 2 && before < 7)
            clocks += batch;
          const bool divide = kind == 2 || kind == 3 || kind == 6 || kind == 7;
          if (divide && zero)
            clocks += before * 2 + latencies[kind] * 2 + 2;
          equal(f.cpu.run_block(start + 10000), true);
          equal(f.cpu.state().clocks - start, clocks);
          equal(f.cpu.read_control(Count), clocks / 4);
          equal(f.cpu.state().gpr[6], before);
          equal(f.cpu.state().gpr[8], 0x1234);
          equal(f.cpu.state().pc, 0xffffffff80003000);
          equal(f.cpu.read_control(Cause), 0);
          if (kind < 8) {
            equal(f.cpu.state().hi, divide ? zero ? 7 : 1 : 0);
            equal(f.cpu.state().lo, divide ? zero ? ~0ull : 2 : zero ? 0 : 21);
          } else {
            equal(f.cpu.state().gpr[7], kind >= 12 ? 8 : (kind & 1) ? zero ? 7 : 4 : zero ? 7 : 10);
          }
        }
      }
    }
  }
}

void edges() {
  for (unsigned kind = 0; kind < 14; ++kind) {
    for (unsigned before = 0; before < 8; ++before) {
      for (bool exceptional : {false, true}) {
        for (bool delay : {false, true}) {
          native_memory::CachedFixture f;
          f.cpu.write_control(Status, 0x34000000);
          const bool overflow = exceptional && kind >= 8;
          f.cpu.state().gpr[4] = !overflow                  ? 7
                                 : kind == 8 || kind == 12  ? 0x7fffffffull
                                 : kind == 9                ? 0x80000000ull
                                 : kind == 10 || kind == 13 ? 0x7fffffffffffffffull
                                                            : 0x8000000000000000ull;
          f.cpu.state().gpr[5] = overflow ? 1 : exceptional ? 0 : 3;
          f.cpu.state().gpr[31] = 0xffffffff80003000;
          for (unsigned word = 0; word < before; ++word)
            f.code(word * 4, i(9, 6, 6, 1), false);
          unsigned word = before;
          if (delay) {
            f.code(word++ * 4, r(8, 31, 0, 0), false);
            f.code(word++ * 4, operations[kind], false);
          } else {
            f.code(word++ * 4, operations[kind], false);
            f.code(word++ * 4, r(8, 31, 0, 0), false);
            f.code(word++ * 4, 0, false);
          }
          f.cpu.set_pc(0xffffffff80001000);
          f.cpu.write_control(Count, 0);
          f.cpu.write_control(Compare, 0xffffffff);
          const auto start = f.cpu.state().clocks;
          const auto fetched = overflow ? before + unsigned(delay) + 1 : word;
          unsigned clocks = 96 * ((fetched + 7) / 8) + before * 2;
          const bool divide = kind == 2 || kind == 3 || kind == 6 || kind == 7;
          if (overflow)
            clocks += 2;
          else {
            clocks += latencies[kind] + (delay ? 4 : 6);
            if (divide && exceptional) {
              if (delay)
                clocks -= 2;
              else
                clocks += before * 2 + latencies[kind] * 2 + 2;
            }
          }
          equal(f.cpu.run_block(start + 10000), true);
          equal(f.cpu.state().clocks - start, clocks);
          equal(f.cpu.read_control(Count), clocks / 4);
          equal(f.cpu.state().gpr[6], before);
          equal(f.cpu.state().pc, overflow ? 0xffffffff80000180ull : 0xffffffff80003000ull);
          equal(f.cpu.read_control(Cause), overflow ? 0x30u | (delay ? 0x80000000u : 0) : 0);
          equal(f.cpu.read_control(Epc), overflow ? 0xffffffff80001000ull + before * 4 : 0);
          if (kind < 8) {
            equal(f.cpu.state().hi, divide ? exceptional ? 7 : 1 : 0);
            equal(f.cpu.state().lo, divide ? exceptional ? ~0ull : 2 : exceptional ? 0 : 21);
          } else {
            equal(f.cpu.state().gpr[7], overflow ? 0 : kind >= 12 ? 8 : (kind & 1) ? 4 : 10);
          }
        }
      }
    }
  }
}

} // namespace

void native_arithmetic_timing_tests() {
  batches();
  edges();
}

} // namespace test
