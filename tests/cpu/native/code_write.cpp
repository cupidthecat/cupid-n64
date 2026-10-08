#include "../../support/test.hpp"
#include "core/memory/instruction_tracker.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct CodeMemory : Bus {
  std::array<std::uint32_t, 4096> words{};
  InstructionTracker tracker{sizeof(words)};
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address / 4)
                                   : std::span<const std::uint32_t>();
  }
  InstructionTracker *instruction_tracker() override {
    return &tracker;
  }
  BusRead read(std::uint32_t address, unsigned bytes) override {
    equal(bytes, 4);
    return {words[address / 4]};
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    std::copy_n(words.begin() + address / 4, output.size(), output.begin());
    return {};
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    equal(bytes, 4);
    words[address / 4] = static_cast<std::uint32_t>(value);
    tracker.invalidate(address, bytes);
    return {};
  }
};

void cache_reuse_tests() {
  struct Result {
    unsigned clocks;
    unsigned value;
    std::uint32_t word;
  };
  constexpr std::array<std::array<Result, 3>, 56> results = {{
      {{{206, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{206, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{206, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{110, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{206, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{206, 1, 612630529u}, {14, 1, 612630529u}, {110, 1, 612630529u}}},
      {{{110, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{110, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{110, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{110, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{14, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{110, 7, 612630535u}, {14, 7, 612630535u}, {110, 8, 612630535u}}},
      {{{110, 1, 612630529u}, {14, 1, 612630529u}, {110, 1, 612630529u}}},
      {{{14, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{206, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{206, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{206, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{110, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{206, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{206, 1, 612630529u}, {110, 1, 612630529u}, {110, 1, 612630529u}}},
      {{{110, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{110, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{110, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{110, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{14, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{110, 7, 612630535u}, {110, 7, 612630535u}, {14, 8, 612630535u}}},
      {{{110, 1, 612630529u}, {110, 1, 612630529u}, {110, 1, 612630529u}}},
      {{{14, 1, 612630535u}, {14, 1, 612630535u}, {14, 1, 612630535u}}},
      {{{110, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{110, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {110, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{14, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{14, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{14, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{14, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{14, 1, 612630529u}, {110, 8, 612630529u}, {110, 8, 612630529u}}},
      {{{14, 1, 612630529u}, {110, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{14, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{206, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{206, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{206, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{206, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{206, 1, 612630529u}, {110, 1, 612630529u}, {110, 1, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{14, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
      {{{110, 1, 612630529u}, {14, 8, 612630529u}, {14, 8, 612630529u}}},
      {{{110, 1, 612630529u}, {110, 1, 612630529u}, {110, 1, 612630529u}}},
      {{{14, 1, 612630529u}, {14, 1, 612630529u}, {14, 1, 612630529u}}},
  }};
  for (bool interpreted : {false, true})
    for (bool fully_tracked : {false, true}) {
      unsigned test_case = 0;
      for (bool little : {false, true})
        for (bool delay : {false, true})
          for (bool warm : {false, true})
            for (unsigned operation : {0u, 8u, 16u, 17u, 20u, 24u, 31u}) {
              CodeMemory memory;
              Cpu cpu(memory);
              memory.tracker.set_fully_tracked(fully_tracked);
              const auto control = c(4, 5, Status);
              const std::array code{i(43, 1, 2, 0),
                                    delay ? i(4, 0, 0, 1) : i(47, 3, operation, 0),
                                    delay ? i(47, 3, operation, 0) : i(9, 4, 4, 1),
                                    delay ? i(9, 4, 4, 1) : control,
                                    control,
                                    0u,
                                    control};
              std::copy(code.begin(), code.end(), memory.words.begin() + 0x2000 / 4);
              cpu.write_control(Status, 0x30000000);
              cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
              cpu.state().gpr[1] = 0xffffffffa0002000ull + (delay ? 12 : 8);
              cpu.state().gpr[2] = i(9, 4, 4, 7);
              cpu.state().gpr[3] = 0xffffffff80002000ull;
              cpu.state().gpr[5] = 0x30000000;
              if (warm)
                cpu.execute(i(47, 3, 20, 0));
              for (unsigned pass = 0; pass < 3; ++pass) {
                const auto config = (little ^ (pass == 1)) ? 0x70066460u : 0x7006e460u;
                cpu.write_control(Config, config);
                cpu.state().gpr[4] = 0;
                cpu.set_pc(0xffffffff80002000ull);
                cpu.write_control(Count, 0);
                cpu.write_control(Compare, 0xffffffff);
                const auto start = cpu.state().clocks;
                for (unsigned batches = 0; cpu.state().pc != 0xffffffff8000201cull && batches < 32;
                     ++batches) {
                  const auto target = cpu.state().clocks + 10000;
                  if (!(interpreted ? cpu.run_interpreted_block(target) : cpu.run_block(target)))
                    cpu.step();
                }
                const auto &result = results[test_case][pass];
                equal(cpu.state().clocks - start, result.clocks);
                equal(cpu.read_control(Count), result.clocks / 4);
                equal(cpu.state().pc, 0xffffffff8000201cull);
                equal(cpu.in_delay_slot(), false);
                equal(cpu.read_control(Cause), 0);
                equal(cpu.read_control(Epc), 0);
                equal(cpu.read_control(Status), 0x30000000);
                equal(cpu.read_control(Config), config);
                equal(memory.words[0x2000 / 4 + (delay ? 3 : 2)], result.word);
                for (unsigned reg = 0; reg < 32; ++reg)
                  equal(cpu.state().gpr[reg], reg == 1    ? 0xffffffffa0002000ull + (delay ? 12 : 8)
                                              : reg == 2  ? i(9, 4, 4, pass == 1 ? 1 : 7)
                                              : reg == 3  ? 0xffffffff80002000ull
                                              : reg == 4  ? result.value
                                              : reg == 5  ? 0x30000000ull
                                              : reg == 29 ? 0xffffffffa4001ff0ull
                                                          : 0);
                cpu.state().gpr[2] = i(9, 4, 4, pass == 0 ? 1 : 7);
              }
              ++test_case;
            }
      equal(test_case, results.size());
    }
}

void cache_continuation_tests() {
  for (bool interpreted : {false, true})
    for (bool little : {false, true})
      for (bool warm : {false, true})
        for (unsigned operation : {0u, 8u, 16u, 17u, 20u, 24u, 31u}) {
          CodeMemory memory;
          Cpu cpu(memory);
          const std::array code{i(47, 3, operation, 0), i(9, 4, 4, 1),
                                (16u << 26) | (4u << 21) | (5u << 16) | (Status << 11)};
          std::copy(code.begin(), code.end(), memory.words.begin() + 0x1000 / 4);
          cpu.write_control(Status, 0x30000000);
          cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
          cpu.state().gpr[3] = 0xffffffff80001000ull;
          cpu.state().gpr[5] = 0x30000000;
          if (warm)
            cpu.execute(i(47, 3, 20, 0));
          cpu.set_pc(0xffffffff80001000ull);
          cpu.write_control(Count, 0);
          cpu.write_control(Compare, 0xffffffff);
          const auto start = cpu.state().clocks;
          equal(interpreted ? cpu.run_interpreted_block(start + 10000)
                            : cpu.run_block(start + 10000),
                true);
          const bool stopped = operation == 24;
          const unsigned clocks = (warm ? 0 : 96) + (stopped ? 98 : operation == 20 ? 102 : 6);
          equal(cpu.state().clocks - start, clocks);
          equal(cpu.read_control(Count), clocks / 4);
          equal(cpu.state().pc, stopped ? 0xffffffff80001004ull : 0xffffffff8000100cull);
          equal(cpu.state().gpr[4], stopped ? 0 : 1);
          equal(cpu.in_delay_slot(), false);
          equal(cpu.read_control(Cause), 0);
          equal(cpu.read_control(Epc), 0);
        }
  for (bool interpreted : {false, true})
    for (bool little : {false, true})
      for (bool warm : {false, true})
        for (auto branch : {0x08000400u, i(4, 0, 0, 0xfffe), i(5, 0, 0, 0xfffe)}) {
          CodeMemory memory;
          Cpu cpu(memory);
          const auto control = (16u << 26) | (4u << 21) | (5u << 16) | (Status << 11);
          const std::array code{i(47, 3, 16, 0), branch, i(9, 4, 4, 1), control};
          std::copy(code.begin(), code.end(), memory.words.begin() + 0x1000 / 4);
          cpu.write_control(Status, 0x30000000);
          cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
          cpu.state().gpr[3] = 0xffffffff80001000ull;
          cpu.state().gpr[5] = 0x30000000;
          if (warm)
            cpu.execute(i(47, 3, 20, 0));
          cpu.set_pc(0xffffffff80001000ull);
          cpu.write_control(Count, 0);
          cpu.write_control(Compare, 0xffffffff);
          const auto start = cpu.state().clocks;
          equal(interpreted ? cpu.run_interpreted_block(start + 4096) : cpu.run_block(start + 4096),
                true);
          const bool fallthrough = (branch >> 26) == 5;
          const unsigned clocks = (warm ? 0 : 96) + (fallthrough ? 8 : 6);
          equal(cpu.state().clocks - start, clocks);
          equal(cpu.read_control(Count), clocks / 4);
          equal(cpu.state().pc, fallthrough ? 0xffffffff80001010ull : 0xffffffff80001000ull);
          equal(cpu.state().gpr[4], 1);
          equal(cpu.in_delay_slot(), false);
          equal(cpu.read_control(Cause), 0);
          equal(cpu.read_control(Epc), 0);
        }
}

} // namespace

void native_code_write_tests() {
  for (bool interpreted : {false, true})
    for (bool fully_tracked : {false, true})
      for (bool little : {false, true})
        for (bool delay : {false, true})
          for (unsigned before = 0; before < 8; ++before)
            for (bool warm : {false, true})
              for (unsigned target = 0; target < 4; ++target)
                for (unsigned value : {1u, 7u}) {
                  if (interpreted && delay)
                    continue;
                  CodeMemory memory;
                  Cpu cpu(memory);
                  memory.tracker.set_fully_tracked(fully_tracked);
                  const auto run = [&](std::uint64_t target) {
                    return interpreted ? cpu.run_interpreted_block(target) : cpu.run_block(target);
                  };
                  std::vector<std::uint32_t> code(before, i(9, 6, 6, 1));
                  if (delay)
                    code.push_back(i(4, 0, 0, 1));
                  code.insert(code.end(), {i(43, 2, 9, 0), i(9, 4, 4, 1), r(8, 31, 0, 0), 0u, 0u});
                  std::copy(code.begin(), code.end(), memory.words.begin() + 0x1000 / 4);
                  const unsigned address = target == 0   ? 0x1004 + before * 4 + (delay ? 4 : 0)
                                           : target == 1 ? 0x1800
                                           : target == 2 ? 0x3000
                                                         : 0x1100;
                  cpu.write_control(Status, 0x30000000);
                  cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
                  cpu.state().gpr[31] = 0xffffffff80007000ull;
                  if (target == 1 || target == 2) {
                    memory.words[address / 4] = r(8, 31, 0, 0);
                    cpu.set_pc(0xffffffff80000000ull | address);
                    equal(run(cpu.state().clocks + 10000), true);
                  }
                  cpu.state().gpr[3] = 0xffffffff80001000ull;
                  if (warm)
                    cpu.execute(i(47, 3, 20, 0));
                  cpu.state().gpr[2] = 0xffffffffa0000000ull | (address ^ (little ? 4u : 0u));
                  cpu.state().gpr[9] = i(9, 4, 4, value);
                  cpu.state().gpr[4] = cpu.state().gpr[6] = 0;
                  cpu.set_pc(0xffffffff80001000ull);
                  cpu.write_control(Count, 0);
                  cpu.write_control(Compare, 0xffffffff);
                  const auto start = cpu.state().clocks;
                  equal(run(start + 10000), true);
                  const bool stopped = delay || target <= 1;
                  const unsigned clocks =
                      (warm ? 0 : 96) + (delay     ? (before == 7 ? 96 : 0) + before * 2 + 2
                                         : stopped ? before * 2 + 2
                                                   : (before >= 5 ? 96 : 0) + before * 4 + 10);
                  if (!interpreted || stopped) {
                    equal(cpu.state().clocks - start, clocks);
                    equal(cpu.read_control(Count), clocks / 4);
                  }
                  equal(cpu.state().pc, stopped
                                            ? 0xffffffff80001004ull + before * 4 + (delay ? 4 : 0)
                                            : 0xffffffff80007000ull);
                  equal(cpu.state().gpr[4], stopped ? 0 : 1);
                  equal(cpu.state().gpr[6], before);
                  equal(cpu.in_delay_slot(), false);
                  equal(cpu.read_control(Cause), 0);
                  equal(cpu.read_control(Epc), 0);
                  equal(memory.words[address / 4], i(9, 4, 4, value));
                }
  cache_continuation_tests();
  cache_reuse_tests();
}

} // namespace test
