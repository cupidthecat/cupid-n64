#include "../rsp/fixture.hpp"
#include "check.hpp"

namespace test::machine_replay {

void rsp_tests() {
  for (bool taken : {false, true})
    for (unsigned budget : {1u, 7u, 31u, 129u}) {
      Input input;
      input.expansion = false;
      input.initial = {{ActionKind::RspIo, 16, 1}};
      const std::uint32_t words[] = {
          i(9, 0, 1, 7), i(4, taken ? 0 : 1, 0, 2), i(9, 0, 2, 9), i(9, 0, 3, 11), 0x08000000, 0};
      signal_code(input, words);
      for (unsigned n = 0; n < 3; ++n)
        input.actions.push_back({ActionKind::RspRun, 0, budget});
      input.actions.push_back({ActionKind::RspIo, 16, 2});
      if (!check(input, "RSP branch, pipeline, halt and interrupt"))
        return;
    }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::RspIo, 16, 1}, {ActionKind::RspLocal, 0, 41}};
    const std::uint32_t words[] = {i(9, 1, 2, 1), 0x08000000, i(35, 0, 1, 0)};
    signal_code(input, words);
    input.actions = {{ActionKind::RspRun, 0, 1},
                     {ActionKind::RspLocal, 0x1000, i(9, 1, 2, 2)},
                     {ActionKind::RspRun, 0, 15},
                     {ActionKind::RspIo, 16, 2},
                     {ActionKind::RspRun, 0, 129}};
    if (!check(input, "RSP code write and active schedule"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::RspIo, 16, 1}};
    for (unsigned lane = 0; lane < 8; ++lane) {
      input.initial.push_back({ActionKind::RspVector, 8 + lane, 0x7000u + lane});
      input.initial.push_back({ActionKind::RspVector, 16 + lane, 0x6000u + lane});
    }
    const std::uint32_t words[] = {vector(16, 3, 1, 2), i(9, 0, 1, 17), vector(0, 4, 3, 2),
                                   i(0x2b, 0, 1, 0),    0x08000000,     0};
    signal_code(input, words);
    input.actions = {{ActionKind::RspRun, 0, 1}, {ActionKind::RspIo, 16, 2}};
    if (!check(input, "RSP vector flags, accumulator and dual issue"))
      return;
  }
  {
    Input input;
    input.expansion = false;
    const std::uint32_t words[] = {i(9, 0, 1, 19), 0x08000000, 0};
    signal_code(input, words);
    input.initial.push_back({ActionKind::RspIo, 0, 0});
    input.initial.push_back({ActionKind::RspIo, 4, 0x2800});
    input.initial.push_back({ActionKind::RspIo, 8, 15});
    input.actions = {{ActionKind::RspRun, 0, 1},
                     {ActionKind::AdvanceClocks, 0, 1024},
                     {ActionKind::RspIo, 16, 1},
                     {ActionKind::CpuSteps, 0, 3}};
    check(input, "RSP DMA and CPU synchronization");
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::RspIo, 16, 0x101}, {ActionKind::RspLocal, 0x1010, 13}};
    input.actions = {{ActionKind::RspRun, 0, 7}};
    const auto difference = compare(input, [](cupid::n64::Console &console, Mode, std::size_t) {
      equal(console.signal().pc(), 20);
      equal(console.signal().clocks(), 8);
      equal(console.signal().status().halted, true);
      equal(console.signal().status().broken, true);
      equal(console.signal().read_io(16) & 0x43, 0x43);
    });
    // Native halt exits retain the pipeline for the compiled delay slot after BREAK.
    equal(difference.has_value(), true);
    if (difference) {
      equal(difference->boundary, 1);
      equal(difference->state.field == "rsp.pipeline.single_issue", true);
      equal(difference->state.expected, 1);
      equal(difference->state.actual, 0);
    }
    input.contracts = {{1, {"rsp.pipeline.single_issue", 0, 1, 0, 1}}};
    check(input, "RSP halt pipeline contract");
    equal(compare(decode(encode(input))).has_value(), false);
    const auto wrong_register =
        compare(input, [](cupid::n64::Console &console, Mode mode, std::size_t) {
          if (mode == Mode::Native)
            console.cpu().state().gpr[7] = 1;
        });
    equal(wrong_register.has_value(), true);
    if (wrong_register) {
      equal(wrong_register->boundary, 1);
      equal(wrong_register->state.field == "cpu.gpr", true);
      equal(wrong_register->state.index, 7);
      equal(wrong_register->state.expected, 0);
      equal(wrong_register->state.actual, 1);
    }
  }
  {
    Input input;
    input.expansion = false;
    input.initial = {{ActionKind::RspIo, 16, 1}, {ActionKind::RspLocal, 0, 41}};
    const std::uint32_t words[] = {i(9, 1, 2, 1), 0x08000000, i(35, 0, 1, 0)};
    signal_code(input, words);
    input.actions = {{ActionKind::RspRun, 0, 1}, {ActionKind::RspRun, 0, 12}};
    const auto difference = compare(input, [](cupid::n64::Console &console, Mode, std::size_t n) {
      equal(console.signal().pc(), 0);
      equal(console.signal().state().gpr[2], n == 1 ? 1 : 42);
      equal(console.signal().read_local(0, 4), 41);
    });
    // A retained native schedule keeps its original load-use cost until IMEM invalidation.
    equal(difference.has_value(), true);
    if (difference) {
      equal(difference->boundary, 2);
      equal(difference->state.field == "rsp.clock", true);
      equal(difference->state.expected, 14);
      equal(difference->state.actual, 11);
    }
    input.contracts = {{2, {"rsp.clock", 0, 14, 11, 8}}};
    check(input, "RSP cached load-use clock contract");
    equal(compare(decode(encode(input))).has_value(), false);
    const auto wrong_register =
        compare(input, [](cupid::n64::Console &console, Mode mode, std::size_t n) {
          if (mode == Mode::Native && n == 2)
            console.cpu().state().gpr[7] = 1;
        });
    equal(wrong_register.has_value(), true);
    if (wrong_register) {
      equal(wrong_register->boundary, 2);
      equal(wrong_register->state.field == "cpu.gpr", true);
      equal(wrong_register->state.index, 7);
      equal(wrong_register->state.expected, 0);
      equal(wrong_register->state.actual, 1);
    }
  }
}

} // namespace test::machine_replay
