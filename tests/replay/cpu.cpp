#include "../cpu/native_memory_fixture.hpp"
#include "check.hpp"
#include <cfenv>

namespace test::machine_replay {
using namespace cupid::n64;

void cpu_tests() {
  for (bool expansion : {false, true}) {
    Input input;
    input.expansion = expansion;
    input.initial = {{ActionKind::CpuRegister, 2, 0xffffffff80002000}};
    const std::uint32_t words[] = {i(9, 1, 1, 1), i(0x2b, 2, 1, 0), i(5, 1, 0, 0xfffd), 0};
    code(input, words);
    input.initial.push_back({ActionKind::CpuInstruction, 0, i(0x2b, 2, 1, 0)});
    for (unsigned n = 0; n < 6; ++n) {
      input.actions.push_back({ActionKind::Controller, 0, 0x1def8030u ^ n});
      input.actions.push_back({ActionKind::CpuBlock, 0, 33});
    }
    if (!check(input, "integer loop and controller input"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::CpuRegister, 8, 0xfffffffe}, {ActionKind::CpuRegister, 10, 2}};
    const std::uint32_t words[] = {c(4, 8, Count),  c(4, 10, Compare), c(0, 9, Count),
                                   c(0, 11, Cause), r(8, 31, 0, 0),    0};
    code(input, words);
    for (unsigned n = 0; n < 3; ++n)
      input.actions.push_back({ActionKind::CpuBlock, 0, 1});
    input.actions.push_back({ActionKind::CpuPc, 0, 0xffffffff80001000});
    input.actions.push_back({ActionKind::CpuBlock, 0, 4096});
    if (!check(input, "Count and Compare transfers and wrap"))
      return;
  }
  for (bool delay : {false, true}) {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::CpuRegister, 2, 0xffffffff80002001}};
    std::vector<std::uint32_t> words;
    if (delay)
      words.push_back(i(4, 0, 0, 2));
    words.push_back(i(0x37, 2, 3, 0));
    words.push_back(r(8, 31, 0, 0));
    words.push_back(0);
    code(input, words);
    input.actions = {{ActionKind::CpuBlock, 0, 1}};
    if (delay) {
      // The exceptional native delay slot returns before its two-clock completion charge.
      const auto difference = compare(input);
      equal(difference.has_value(), true);
      if (difference) {
        equal(difference->boundary, 1);
        equal(difference->state.field == "events.clock", true);
        equal(difference->state.expected, 102);
        equal(difference->state.actual, 100);
      }
      // Device phases follow the omitted charge; halted RSP scheduling crosses an idle quantum.
      const Difference contracts[] = {{1, {"events.clock", 0, 102, 100, 4}},
                                      {1, {"pif.clock", 0, 0x13f9a, 0x13f9c, 8}},
                                      {1, {"rsp.clock", 0, 154, 28, 8}},
                                      {1, {"rdp.clock", 0, 0xb2d057a, 0xb2d057c, 8}},
                                      {1, {"vi.clock", 0, 0x1e69, 0x1e6b, 8}},
                                      {1, {"audio.clock", 0, 0x1035, 0x1037, 8}},
                                      {1, {"cpu.clocks", 0, 102, 100, 8}},
                                      {1, {"cpu.count_ticks", 0, 51, 50, 8}},
                                      {1, {"cpu.count_clock", 0, 102, 100, 8}},
                                      {1, {"machine.synchronized_clock", 0, 102, 100, 8}},
                                      {1, {"output.audio.sample[0].clocks", 0, 102, 100, 8}}};
      check_contracts(input, contracts, [](Console &console, Mode, std::size_t) {
        equal(console.cpu().read_control(Cause), 0x80000010);
        equal(console.cpu().read_control(Epc), 0xffffffff80001000);
        equal(console.cpu().read_control(BadVAddr), 0xffffffff80002001);
        equal(console.cpu().read_control(Status) & 2, 2);
      });
      const auto wrong_exception = compare(
          input,
          [](Console &console, Mode mode, std::size_t) {
            if (mode == Mode::Native)
              console.cpu().write_control(Epc, 0xffffffff80001004);
          },
          contracts);
      equal(wrong_exception.has_value(), true);
      if (wrong_exception) {
        equal(wrong_exception->boundary, 1);
        equal(wrong_exception->state.field == "cpu.cp0", true);
        equal(wrong_exception->state.index, Epc);
        equal(wrong_exception->state.expected, 0xffffffff80001000);
        equal(wrong_exception->state.actual, 0xffffffff80001004);
      }
    } else if (!check(input, "native address fallback"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::CpuRegister, 2, 0xffffffff80001000},
                     {ActionKind::CpuRegister, 4, 0x34000010}};
    const std::uint32_t words[] = {c(4, 4, Status), r(0x2d, 1, 2, 3), r(8, 31, 0, 0), 0};
    code(input, words);
    input.actions = {{ActionKind::CpuBlock, 0, 1}, {ActionKind::CpuBlock, 0, 4096}};
    if (!check(input, "execution mode change"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::CpuRegister, 2, 0xffffffff80001000}};
    const std::uint32_t words[] = {i(9, 0, 4, 17), r(8, 31, 0, 0), 0};
    code(input, words);
    input.actions = {{ActionKind::CpuBlock, 0, 1},
                     {ActionKind::WriteMemory, 0x1000, i(9, 0, 4, 42)},
                     {ActionKind::CpuInstruction, 0, i(47, 2, 0, 0)},
                     {ActionKind::CpuPc, 0, 0xffffffff80001000},
                     {ActionKind::CpuBlock, 0, 1}};
    if (!check(input, "code write and instruction-cache invalidation"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    const std::uint32_t words[] = {i(9, 0, 4, 17), r(8, 31, 0, 0), 0};
    code(input, words);
    input.initial.push_back({ActionKind::WriteMemory, 0x04600000, 0x2800});
    input.initial.push_back({ActionKind::WriteMemory, 0x04600004, 0x10000000});
    input.initial.push_back({ActionKind::WriteMemory, 0x0460000c, 31});
    input.actions = {{ActionKind::CpuBlock, 0, 1},
                     {ActionKind::AdvanceClocks, 0, 10'000},
                     {ActionKind::CpuControl, Compare, 10},
                     {ActionKind::CpuControl, Count, 0},
                     {ActionKind::CpuControl, Status, 0x34008001},
                     {ActionKind::AdvanceClocks, 0, 40},
                     {ActionKind::CpuBlock, 0, 1}};
    if (!check(input, "pending DMA and timer interrupt"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::CpuFloating, 1, 0x3ff8000000000000},
                     {ActionKind::CpuFloating, 2, 0x4004000000000000}};
    const auto add = (17u << 26) | (17u << 21) | (2u << 16) | (1u << 11) | (3u << 6);
    const std::uint32_t words[] = {add, r(8, 31, 0, 0), 0};
    code(input, words);
    input.actions = {{ActionKind::CpuBlock, 0, 1}};
    const auto rounding = std::fegetround();
    std::fesetround(FE_UPWARD);
    const bool matches = check(input, "fixed host floating-point environment");
    equal(std::fegetround(), FE_UPWARD);
    std::fesetround(rounding);
    if (!matches)
      return;
  }
}

void timing_tests() {
  using native_memory::CachedFixture;
  for (unsigned mode = 0; mode < 3; ++mode) {
    CachedFixture fixture;
    const std::uint32_t words[] = {c(0, 8, Cause), c(0, 9, Count), c(0, 10, Cause), r(8, 31, 0, 0),
                                   0};
    fixture.cpu.write_control(Count, 0);
    fixture.cpu.write_control(Compare, 1);
    fixture.cpu.state().gpr[31] = 0xffffffff80003000;
    for (unsigned n = 0; n < std::size(words); ++n)
      fixture.code(n * 4, words[n]);
    if (mode == 0) {
      for (unsigned n = 0; n < std::size(words); ++n) {
        fixture.cpu.step();
        fixture.cpu.synchronize_timer();
      }
    } else {
      const auto target = fixture.cpu.state().clocks + 4096;
      equal(mode == 1 ? fixture.cpu.run_interpreted_block(target) : fixture.cpu.run_block(target),
            true);
      fixture.cpu.synchronize_timer();
    }
    equal(fixture.cpu.state().clocks, 106);
    equal(fixture.cpu.state().gpr[9], mode == 0 ? 25 : 24);
    equal(fixture.cpu.state().gpr[10], 0x8000);
    equal(fixture.cpu.read_control(Count), 26);
  }
  for (unsigned mode = 0; mode < 3; ++mode) {
    CachedFixture fixture;
    fixture.code(0, 0x1000ffff);
    fixture.code(4, 0);
    const auto target = fixture.cpu.state().clocks + 1;
    if (mode == 0) {
      fixture.cpu.step();
      fixture.cpu.step();
    } else
      equal(mode == 1 ? fixture.cpu.run_interpreted_block(target) : fixture.cpu.run_block(target),
            true);
    // Cold fill: 96 clocks. Two scalar fetches: 4. Block scheduling adds 126 for the self-loop.
    equal(fixture.cpu.state().clocks, mode == 0 ? 100 : 226);
    equal(fixture.cpu.state().pc, 0xffffffff80001000);
  }
  for (unsigned mode = 0; mode < 2; ++mode) {
    CachedFixture fixture;
    fixture.cpu.state().gpr[2] = 0xffffffff80000200;
    fixture.cpu.state().gpr[31] = 0xffffffff80003000;
    fixture.code(0, 0);
    fixture.code(4, i(43, 2, 1, 0));
    fixture.code(8, r(8, 31, 0, 0));
    fixture.code(12, 0);
    equal(mode == 0 ? fixture.cpu.run_interpreted_block(1) : fixture.cpu.run_block(1), true);
    // Native cache-miss helpers charge the pending batch before resuming its deferred charge.
    equal(fixture.cpu.state().clocks, mode == 0 ? 184 : 188);
    equal(fixture.cpu.state().pc, 0xffffffff80003000);
  }
}

} // namespace test::machine_replay
