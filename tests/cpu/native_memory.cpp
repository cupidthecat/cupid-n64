#include "../support/test.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct CachedMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    transfers.push_back({address, static_cast<unsigned>(output.size() * 4), false, 0});
    if (success)
      for (unsigned n = 0; n < output.size(); ++n)
        output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> input) override {
    for (unsigned n = 0; n < input.size(); ++n) {
      transfers.push_back({address + n * 4, 4, true, input[n]});
      if (success)
        words[((address >> 2) + n) % words.size()] = input[n];
    }
    return {clocks, success};
  }
};

struct CachedFixture {
  CachedMemory memory;
  Cpu cpu{memory};
  CachedFixture() {
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(0xffffffff80001000);
    for (unsigned n = 0; n < 1024; ++n)
      memory.words[n] = 0x8091a2b3 ^ (n * 0x172345);
  }
  void code(unsigned offset, std::uint32_t instruction, bool little) {
    memory.words[(1024 + offset / 4) ^ unsigned(little)] = instruction;
  }
};

void compare(CachedFixture &actual, CachedFixture &expected) {
  const auto &a = actual.cpu.state();
  const auto &b = expected.cpu.state();
  equal(a.pc, b.pc);
  equal(a.clocks, b.clocks);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(a.gpr[reg], b.gpr[reg]);
    equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
  equal(actual.memory.transfers.size(), expected.memory.transfers.size());
  for (unsigned n = 0;
       n < std::min(actual.memory.transfers.size(), expected.memory.transfers.size()); ++n) {
    const auto &x = actual.memory.transfers[n];
    const auto &y = expected.memory.transfers[n];
    equal(x.address, y.address);
    equal(x.bytes, y.bytes);
    equal(x.store, y.store);
    equal(x.value, y.value);
  }
  for (unsigned n = 0; n < actual.memory.words.size(); ++n)
    equal(actual.memory.words[n], expected.memory.words[n]);
}

} // namespace

void native_memory_tests() {
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
          fixture->code(0, i(35, 1, 2, 0), false);
          fixture->code(4, i(43, 1, 2, 4), false);
          fixture->code(8, 0x08000400, false);
          fixture->code(12, i(35, 1, 3, 8), false);
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
        compare(actual, expected);
        equal(actual_requests, expected_requests);
        equal(actual_limit, expected_limit);
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
            fixture->code(0, layout == 1 ? 0x08000400 : 0, little);
            fixture->code(4, i(operation, 1, 2, 0), little);
            fixture->code(8, i(35, 1, 3, 4), little);
            fixture->code(12, i(43, 1, 3, 8), little);
            fixture->code(16, 0x08000400, little);
            fixture->code(20, i(operation, 1, 2, 0), little);
            if (layout == 2) {
              fixture->cpu.step();
              fixture->cpu.set_pc(0xffffffff80001000);
              fixture->memory.success = false;
              fixture->cpu.state().gpr[1] = address + 16;
            }
          }
          const std::uint64_t limit = 0;
          equal(actual.cpu.run_block(limit), expected.cpu.run_interpreted_block(limit));
          compare(actual, expected);
          for (auto *fixture : {&actual, &expected}) {
            fixture->memory.success = true;
            fixture->cpu.state().gpr[4] = 0xffffffff80000200;
            fixture->cpu.execute(i(47, 4, 21, 0));
          }
          compare(actual, expected);
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
              fixture->code(0, i(operation, 1, target, 0xfff9), little);
              fixture->code(4, 0x08000400, little);
              fixture->code(8, 0, little);
            }
            const std::uint64_t limit = 0;
            equal(actual.cpu.run_block(limit), expected.cpu.run_interpreted_block(limit));
            compare(actual, expected);
            for (auto *fixture : {&actual, &expected}) {
              fixture->cpu.state().gpr[4] = 0xffffffff80000200;
              for (unsigned offset = 0; offset < 32; offset += 4)
                fixture->cpu.execute(i(35, 4, 5, static_cast<std::uint16_t>(offset)));
              fixture->cpu.execute(i(47, 4, 21, 0));
              fixture->cpu.execute(i(47, 4, 21, 16));
            }
            compare(actual, expected);
          }
        }
      }
    }
  }
}

} // namespace test
