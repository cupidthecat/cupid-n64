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
            f.code(word * 4, i(9, 6, 6, 1));
          f.code(before * 4, operations[kind]);
          f.code((before + 1) * 4, i(35, 2, 8, 0));
          f.code((before + 2) * 4, r(8, 31, 0, 0));
          f.code((before + 3) * 4, 0);
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
            f.code(word * 4, i(9, 6, 6, 1));
          unsigned word = before;
          if (delay) {
            f.code(word++ * 4, r(8, 31, 0, 0));
            f.code(word++ * 4, operations[kind]);
          } else {
            f.code(word++ * 4, operations[kind]);
            f.code(word++ * 4, r(8, 31, 0, 0));
            f.code(word++ * 4, 0);
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

void conditional_delays() {
  for (unsigned function : {26u, 27u, 30u, 31u})
    for (unsigned denominator : {0u, 3u})
      for (unsigned before : {0u, 1u, 7u})
        for (bool taken : {false, true})
          for (bool expired : {false, true})
            for (bool warm : {false, true}) {
              native_memory::CachedFixture f;
              f.cpu.write_control(Status, 0x34000000);
              f.cpu.state().gpr[2] = 5;
              f.cpu.state().gpr[3] = denominator;
              f.cpu.state().gpr[12] = taken ? 0 : 1;
              f.cpu.state().gpr[31] = 0xffffffff80003000;
              unsigned word = 0;
              for (; word < before; ++word)
                f.code(word * 4, i(9, 10, 10, 1));
              const auto displacement = (0x3000 - (0x1000 + word * 4 + 4)) / 4;
              f.code(word++ * 4, i(4, 11, 12, static_cast<std::uint16_t>(displacement)));
              f.code(word++ * 4, r(function, 2, 3, 0));
              f.code(word++ * 4, r(8, 31, 0, 0));
              f.code(word++ * 4, 0);
              if (warm)
                for (unsigned line = 0; line < (word + 7) / 8; ++line) {
                  f.cpu.state().gpr[1] = 0xffffffff80001000ull + line * 32;
                  f.cpu.execute(i(47, 1, 20, 0));
                }
              f.cpu.set_pc(0xffffffff80001000);
              f.cpu.write_control(Count, 0);
              f.cpu.write_control(Compare, expired ? 0 : 0xffffffff);
              const auto start = f.cpu.state().clocks;
              equal(f.cpu.run_block(expired ? start : start + 4096), true);
              const bool tail = !taken && !expired;
              const auto fetched = before + 2 + (tail ? 2 : 0);
              const auto clocks = before * 2 + (function >= 30 ? 136 : 72) + (denominator ? 4 : 2) +
                                  (tail ? 4 : 0) + (warm ? 0 : 96 * ((fetched + 7) / 8));
              equal(f.cpu.state().clocks - start, clocks);
              equal(f.cpu.read_control(Count), clocks / 4);
              equal(f.cpu.state().hi, denominator ? 2 : 5);
              equal(f.cpu.state().lo, denominator ? 1 : ~0ull);
              equal(f.cpu.state().gpr[10], before);
              equal(f.cpu.state().pc, taken || !expired ? 0xffffffff80003000ull
                                                        : 0xffffffff80001000ull + (before + 2) * 4);
              equal(f.cpu.read_control(Cause), 0);
              equal(f.cpu.read_control(Epc), 0);
            }
}

} // namespace

void native_arithmetic_timing_tests() {
  batches();
  edges();
  conditional_delays();
}

} // namespace test
