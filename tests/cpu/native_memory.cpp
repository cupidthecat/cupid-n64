#include "native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
using namespace native_memory;

void native_memory_tests() {
  for (bool little : {false, true}) {
    for (unsigned operation : {32u, 33u, 35u, 36u, 37u, 39u, 40u, 41u, 43u, 55u, 63u}) {
      for (unsigned timer : {1u, 2u, 3u}) {
        CachedFixture actual;
        CachedFixture expected;
        unsigned actual_requests = 0;
        unsigned expected_requests = 0;
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
          fixture->cpu.state().gpr[1] = 0xffffffff80000200;
          fixture->cpu.state().gpr[2] = 0x8394a5b6c7d8e9fa;
          fixture->code(0, i(operation, 1, 2, 0));
          fixture->code(4, i(35, 1, 3, 8));
          fixture->code(8, c(4, 5, Status));
          fixture->cpu.state().gpr[5] = 0x30000000;
          fixture->cpu.run_interpreted_block(0);
          fixture->cpu.set_pc(0xffffffff80001000);
          fixture->cpu.write_control(Count, 0);
          fixture->cpu.write_control(Compare, timer);
          fixture->cpu.write_control(Status, 0x30008001);
        }
        actual.cpu.connect_sync([&] { ++actual_requests; });
        expected.cpu.connect_sync([&] { ++expected_requests; });
        const auto limit = actual.cpu.state().clocks + 64;
        equal(actual.cpu.run_block(limit), expected.cpu.run_interpreted_block(limit));
        compare(actual, expected, false);
        equal(actual_requests, expected_requests);
      }
    }
  }
  for (auto count : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
    for (auto timer : {0u, 1u, 2u, 0xffffffffu}) {
      for (bool odd : {false, true}) {
        CachedFixture actual;
        CachedFixture expected;
        std::uint64_t actual_limit = 0;
        std::uint64_t expected_limit = 0;
        unsigned actual_requests = 0;
        unsigned expected_requests = 0;
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.state().gpr[1] = 0xffffffff80000200;
          fixture->code(0, i(35, 1, 2, 0));
          fixture->code(4, i(43, 1, 2, 4));
          fixture->code(8, 0x08000400);
          fixture->code(12, i(35, 1, 3, 8));
          fixture->cpu.run_interpreted_block(0);
          fixture->cpu.advance_clocks(unsigned(odd));
          fixture->cpu.write_control(Count, count);
          fixture->cpu.write_control(Compare, timer);
          fixture->cpu.write_control(Status, 0x30008001);
        }
        actual.cpu.connect_sync([&] {
          ++actual_requests;
          actual_limit = actual.cpu.state().clocks;
        });
        expected.cpu.connect_sync([&] {
          ++expected_requests;
          expected_limit = expected.cpu.state().clocks;
        });
        equal(actual.cpu.run_block(actual_limit),
              expected.cpu.run_interpreted_block(expected_limit));
        compare(actual, expected, false);
        equal(actual_requests, expected_requests);
        equal(actual_limit != 0, expected_limit != 0);
        equal(actual_limit <= actual.cpu.state().clocks, true);
      }
    }
  }
  for (bool little : {false, true}) {
    for (unsigned layout : {0u, 1u, 2u}) {
      for (auto address : {0xffffffff80000200ull, 0xffffffff80002200ull, 0xffffffffa0000200ull,
                           0x0000000080000200ull, 0x200ull}) {
        for (unsigned operation : {32u, 33u, 35u, 36u, 37u, 39u, 40u, 41u, 43u, 55u, 63u}) {
          CachedFixture actual;
          CachedFixture expected;
          for (auto *fixture : {&actual, &expected}) {
            fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
            fixture->cpu.state().gpr[1] = 0xffffffff80000200;
            fixture->cpu.state().gpr[2] = 0x8394a5b6c7d8e9fa;
            fixture->cpu.execute(i(43, 1, 2, 0));
            fixture->cpu.state().gpr[1] = address;
            fixture->cpu.set_pc(0xffffffff80001000);
            fixture->code(0, layout == 1 ? 0x08000400 : 0);
            fixture->code(4, i(operation, 1, 2, 0));
            fixture->code(8, i(35, 1, 3, 4));
            fixture->code(12, i(43, 1, 3, 8));
            fixture->code(16, 0x08000400);
            fixture->code(20, i(operation, 1, 2, 0));
            if (layout == 2) {
              fixture->cpu.step();
              fixture->cpu.set_pc(0xffffffff80001000);
              fixture->memory.success = false;
              fixture->cpu.state().gpr[1] = address + 16;
            }
          }
          const std::uint64_t limit = 0;
          equal(actual.cpu.run_block(limit), expected.cpu.run_interpreted_block(limit));
          compare(actual, expected, false);
          for (auto *fixture : {&actual, &expected}) {
            fixture->memory.success = true;
            fixture->cpu.state().gpr[4] = 0xffffffff80000200;
            fixture->cpu.execute(i(47, 4, 21, 0));
          }
          compare(actual, expected, false);
        }
      }
    }
  }
  for (bool little : {false, true}) {
    for (bool hit : {false, true}) {
      for (unsigned operation : {32u, 33u, 35u, 36u, 37u, 39u, 40u, 41u, 43u, 55u, 63u}) {
        for (unsigned offset : {0u, 1u, 2u, 3u, 7u, 12u, 15u, 16u}) {
          for (unsigned target : {0u, 1u, 2u}) {
            CachedFixture actual;
            CachedFixture expected;
            for (auto *fixture : {&actual, &expected}) {
              fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
              fixture->cpu.state().gpr[1] = 0xffffffff80000200;
              fixture->cpu.state().gpr[2] = 0x8394a5b6c7d8e9fa;
              if (hit)
                fixture->cpu.execute(i(35, 1, 3, static_cast<std::uint16_t>(offset & ~15u)));
              fixture->cpu.state().gpr[1] += offset + 7;
              fixture->cpu.set_pc(0xffffffff80001000);
              fixture->code(0, i(operation, 1, target, 0xfff9));
              fixture->code(4, 0x08000400);
              fixture->code(8, 0);
            }
            const std::uint64_t limit = 0;
            equal(actual.cpu.run_block(limit), expected.cpu.run_interpreted_block(limit));
            compare(actual, expected, false);
            for (auto *fixture : {&actual, &expected}) {
              fixture->cpu.state().gpr[4] = 0xffffffff80000200;
              for (unsigned offset = 0; offset < 32; offset += 4)
                fixture->cpu.execute(i(35, 4, 5, static_cast<std::uint16_t>(offset)));
              fixture->cpu.execute(i(47, 4, 21, 0));
              fixture->cpu.execute(i(47, 4, 21, 16));
            }
            compare(actual, expected, false);
          }
        }
      }
    }
  }
}

} // namespace test
