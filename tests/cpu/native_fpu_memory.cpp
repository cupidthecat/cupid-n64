#include "native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
using namespace native_memory;
namespace {

void prepare(CachedFixture &fixture, bool little, bool fr, bool enabled) {
  fixture.cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
  fixture.cpu.write_control(Status, 0x10000000 | (unsigned(fr) << 26) | (unsigned(enabled) << 29));
  fixture.cpu.state().gpr[1] = 0xffffffff80000200;
  fixture.cpu.state().gpr[2] = 0x0123456789abcdef;
  fixture.cpu.state().fcr31 = 0x0183ffff;
  for (unsigned reg = 0; reg < 32; ++reg)
    fixture.cpu.state().fpr[reg] = 0xfedcba9876543210ull ^ (0x9384756a01020304ull * reg);
}

void flush(CachedFixture &fixture) {
  fixture.memory.success = true;
  fixture.cpu.state().gpr[4] = 0xffffffff80000200;
  for (unsigned offset : {0u, 16u}) {
    fixture.cpu.execute(i(35, 4, 5, static_cast<std::uint16_t>(offset)));
    fixture.cpu.execute(i(35, 4, 6, static_cast<std::uint16_t>(offset + 4)));
    fixture.cpu.execute(i(47, 4, 21, static_cast<std::uint16_t>(offset)));
  }
}

void run(CachedFixture &actual, CachedFixture &expected) {
  const std::uint64_t target = 0;
  equal(actual.cpu.run_block(target), true);
  equal(expected.cpu.run_interpreted_block(target), true);
  compare(actual, expected, false);
}

void transfers() {
  for (bool little : {false, true}) {
    for (bool fr : {false, true}) {
      for (bool hit : {false, true}) {
        for (unsigned operation : {49u, 53u, 57u, 61u}) {
          for (unsigned offset : {0u, 1u, 4u, 7u, 8u, 12u, 15u, 16u}) {
            for (unsigned reg : {0u, 1u, 2u, 3u, 30u, 31u}) {
              CachedFixture actual, expected;
              for (auto *fixture : {&actual, &expected}) {
                prepare(*fixture, little, fr, true);
                if (hit)
                  fixture->cpu.execute(i(35, 1, 3, static_cast<std::uint16_t>(offset & ~15u)));
                fixture->cpu.state().gpr[1] += offset + 7;
                fixture->cpu.set_pc(0xffffffff80001000);
                fixture->code(0, i(operation, 1, reg, 0xfff9));
                fixture->code(4, 0x08000400);
                fixture->code(8, 0);
              }
              run(actual, expected);
              flush(actual);
              flush(expected);
              compare(actual, expected, false);
            }
          }
        }
      }
    }
  }
}

void faults() {
  for (bool little : {false, true}) {
    for (bool fr : {false, true}) {
      for (bool enabled : {false, true}) {
        for (unsigned layout : {0u, 1u, 2u, 3u}) {
          for (auto address : {0xffffffff80000200ull, 0xffffffff80002200ull, 0xffffffffa0000200ull,
                               0x80000200ull, 0x200ull, 0xffffffff80000201ull}) {
            for (unsigned operation : {49u, 53u, 57u, 61u}) {
              CachedFixture actual, expected;
              for (auto *fixture : {&actual, &expected}) {
                prepare(*fixture, little, fr, enabled);
                fixture->cpu.execute(i(43, 1, 2, 0));
                fixture->cpu.state().gpr[1] = address;
                fixture->code(0, layout == 1 ? 0x08000400 : 0);
                fixture->code(4, i(operation, 1, 3, 0));
                fixture->code(8, i(49, 1, 4, 4));
                fixture->code(12, 0x08000400);
                fixture->code(16, i(57, 1, 5, 8));
                if (layout >= 2) {
                  fixture->cpu.set_pc(0xffffffff80001000);
                  fixture->cpu.step();
                  fixture->memory.success = false;
                  if (layout == 2)
                    fixture->cpu.state().gpr[1] = address + 16;
                }
                fixture->cpu.set_pc(0xffffffff80001000);
              }
              run(actual, expected);
              if (!enabled) {
                equal((actual.cpu.read_control(Cause) >> 2) & 31, 11);
                equal((actual.cpu.read_control(Cause) >> 28) & 3, 1);
                equal(actual.cpu.read_control(BadVAddr), 0);
              }
              flush(actual);
              flush(expected);
              compare(actual, expected, false);
            }
          }
        }
      }
    }
  }
}

void modes() {
  for (bool little : {false, true}) {
    for (unsigned operation : {49u, 53u, 57u, 61u}) {
      CachedFixture actual, expected;
      for (auto *fixture : {&actual, &expected}) {
        prepare(*fixture, little, false, true);
        fixture->code(0, i(operation, 1, 1, 0));
        fixture->code(4, 0x08000400);
        fixture->code(8, i(operation, 1, 0, 8));
      }
      // Revisit each layout after compiling the same addresses in the other modes.
      for (unsigned mode : {0u, 1u, 2u, 3u, 2u, 0u, 3u, 1u}) {
        for (auto *fixture : {&actual, &expected}) {
          prepare(*fixture, little, mode & 1, mode & 2);
          fixture->cpu.set_pc(0xffffffff80001000);
        }
        run(actual, expected);
      }
      flush(actual);
      flush(expected);
      compare(actual, expected, false);
    }
  }
  for (bool little : {false, true}) {
    for (bool fr : {false, true}) {
      CachedFixture actual, expected;
      for (auto *fixture : {&actual, &expected}) {
        prepare(*fixture, little, fr, true);
        fixture->cpu.state().gpr[3] = 0x30000000 | (unsigned(!fr) << 26);
        fixture->code(0, i(49, 1, 1, 0));
        fixture->code(4, c(4, 3, Status));
        fixture->code(8, i(49, 1, 1, 4));
        fixture->code(12, 0x08000400);
        fixture->code(16, i(61, 1, 1, 8));
      }
      for (unsigned repeat = 0; repeat < 4; ++repeat) {
        run(actual, expected);
        run(actual, expected);
      }
      flush(actual);
      flush(expected);
      compare(actual, expected, false);
    }
  }
}

void mapped_memory() {
  for (bool little : {false, true}) {
    for (bool fr : {false, true}) {
      for (unsigned operation : {49u, 53u, 57u, 61u}) {
        for (unsigned flags : {0u, 1u, 3u, 7u, 0x17u}) {
          CachedFixture actual, expected;
          for (auto *fixture : {&actual, &expected}) {
            prepare(*fixture, little, fr, true);
            fixture->cpu.write_control(Index, 0);
            fixture->cpu.write_control(EntryHi, 0x4000);
            fixture->cpu.write_control(EntryLo0, flags);
            fixture->cpu.write_control(EntryLo1, 0x40 | flags);
            fixture->cpu.execute(co(2));
            fixture->cpu.state().gpr[1] = 0x4200;
            fixture->code(0, i(operation, 1, 1, 0));
            fixture->code(4, 0x08000400);
            fixture->code(8, i(operation, 1, 0, 8));
            fixture->cpu.set_pc(0xffffffff80001000);
          }
          run(actual, expected);
          if (!(flags & 2))
            equal((actual.cpu.read_control(Cause) >> 2) & 31, operation & 8 ? 3 : 2);
          else if ((operation & 8) && !(flags & 4))
            equal((actual.cpu.read_control(Cause) >> 2) & 31, 1);
          else
            equal((actual.cpu.read_control(Cause) >> 2) & 31, 0);
          flush(actual);
          flush(expected);
          compare(actual, expected, false);
        }
      }
    }
  }
}

void timing() {
  for (unsigned operation : {49u, 53u, 57u, 61u}) {
    for (bool little : {false, true}) {
      for (bool fr : {false, true}) {
        for (unsigned parity : {0u, 1u, 3u}) {
          for (unsigned count : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
            for (unsigned timer : {0u, 1u, 2u, 0xffffffffu}) {
              CachedFixture actual, expected;
              std::uint64_t actual_limit = 0, expected_limit = 0;
              unsigned actual_sync = 0, expected_sync = 0;
              for (auto *fixture : {&actual, &expected}) {
                prepare(*fixture, little, fr, true);
                fixture->memory.clocks = parity;
                fixture->code(0, i(operation, 1, 1, 0));
                fixture->code(4, i(operation, 1, 0, 8));
                fixture->code(8, i(4, 0, 0, 0xfffd));
                fixture->code(12, i(operation, 1, 3, 0));
                fixture->cpu.run_interpreted_block(0);
                fixture->cpu.advance_clocks(parity);
                fixture->cpu.write_control(Count, count);
                fixture->cpu.write_control(Compare, timer);
                fixture->cpu.write_control(Status, 0x30008001 | (unsigned(fr) << 26));
              }
              actual.cpu.connect_sync([&] {
                ++actual_sync;
                actual_limit = actual.cpu.state().clocks;
              });
              expected.cpu.connect_sync([&] {
                ++expected_sync;
                expected_limit = expected.cpu.state().clocks;
              });
              equal(actual.cpu.run_block(actual_limit), true);
              equal(expected.cpu.run_interpreted_block(expected_limit), true);
              compare(actual, expected, false);
              equal(actual_limit != 0, expected_limit != 0);
              equal(actual_limit <= actual.cpu.state().clocks, true);
              equal(actual_sync, expected_sync);
            }
          }
        }
      }
    }
  }
}

} // namespace

void native_fpu_memory_tests() {
  transfers();
  faults();
  modes();
  mapped_memory();
  timing();
}

} // namespace test
