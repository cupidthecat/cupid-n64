#include "../../rsp/fixture.hpp"
#include "fixture.hpp"
#include <algorithm>
#include <array>

namespace tracing_test {
namespace {

constexpr std::uint64_t start = 0xffffffff80001000;
constexpr std::array program{test::i(9, 0, 1, 2), test::i(9, 0, 2, 3),  test::i(4, 1, 2, 3),
                             test::i(9, 0, 3, 7), test::i(21, 1, 1, 2), test::i(9, 0, 4, 8),
                             test::i(9, 0, 5, 9), std::uint32_t(12)};

void run_cpu(Console &console, unsigned mode) {
  for (unsigned n = 0; n < 32 && console.cpu().state().pc >= start &&
                       console.cpu().state().pc < start + program.size() * 4;
       ++n) {
    const std::uint64_t target = 10000;
    if (mode == 0)
      console.cpu().step();
    else if (mode == 1) {
      if (!console.cpu().run_interpreted_block(target))
        console.cpu().step();
    } else if (!console.cpu().run_block(target))
      console.cpu().step();
  }
}

void cpu_execution() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    auto plain = machine(), traced = machine();
    code(*plain, program);
    code(*traced, program);
    traced->tracing().configure({.mask = all_traces});
    const auto before = CoreState::capture(*traced);
    equal(CoreState::capture(*plain) == before, true);
    run_cpu(*plain, mode);
    run_cpu(*traced, mode);
    equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
    const auto records = traced->tracing().records();
    std::vector<TraceRecord> instructions;
    for (const auto &record : records)
      if (record.kind == TraceKind::CpuInstruction)
        instructions.push_back(record);
    constexpr std::array<unsigned, 7> indices{0, 1, 2, 3, 4, 6, 7};
    equal(instructions.size(), indices.size());
    for (unsigned n = 0; n < std::min(instructions.size(), indices.size()); ++n) {
      equal(instructions[n].pc, start + indices[n] * 4);
      equal(instructions[n].instruction, program[indices[n]]);
      equal(instructions[n].delay_slot, indices[n] == 3);
      if (n)
        equal(instructions[n].clock > instructions[n - 1].clock, true);
    }
    const auto exception = std::find_if(records.begin(), records.end(), [](const auto &record) {
      return record.kind == TraceKind::Exception;
    });
    equal(exception != records.end(), true);
    if (exception != records.end()) {
      equal(exception->pc, start + 28);
      equal(exception->code, static_cast<unsigned>(Exception::Syscall));
    }
    const auto state = CoreState::capture(*traced);
    CoreState::restore(*traced, state);
    equal(CoreState::capture(*traced) == state, true);
    equal(traced->tracing().config().mask, all_traces);
    traced->tracing().stop();
    traced->cpu().set_pc(start);
    const auto count = traced->tracing().records().size();
    run_cpu(*traced, mode);
    equal(traced->tracing().records().size(), count);
  }
  for (unsigned mode = 0; mode < 3; ++mode) {
    auto plain = machine(), traced = machine();
    code(*plain, program);
    code(*traced, program);
    traced->tracing().configure(
        {.mask = all_traces, .capacity = 4, .cpu = {start + 12, start + 24}});
    run_cpu(*plain, mode);
    run_cpu(*traced, mode);
    equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
    const auto records = traced->tracing().records();
    equal(records.size(), 3);
    for (const auto &record : records)
      equal(record.pc >= start + 12 && record.pc <= start + 24, true);
  }
  for (unsigned operation : {24u, 25u, 32u, 33u, 35u, 36u, 37u, 40u, 41u, 43u, 47u, 49u, 57u}) {
    auto plain = machine(), traced = machine();
    const std::array words{test::i(9, 0, 3, 1), test::i(operation, 1, 2, 0), std::uint32_t(12)};
    for (auto *console : {plain.get(), traced.get()}) {
      code(*console, words);
      console->cpu().state().gpr[1] = 0xffffffff80002000;
      console->cpu().state().gpr[2] = 0x1234;
    }
    traced->tracing().configure({.mask = all_traces});
    const std::uint64_t target = 0;
    equal(plain->cpu().run_block(target), true);
    equal(traced->cpu().run_block(target), true);
    equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
    equal(traced->tracing().records().empty(), false);
  }
  // An internal branch loop executes the same compiled block repeatedly.
  auto plain = machine(), traced = machine();
  const std::array loop{test::i(9, 1, 1, 1), test::i(5, 1, 2, 0xfffe), std::uint32_t(0),
                        std::uint32_t(12)};
  for (auto *console : {plain.get(), traced.get()}) {
    code(*console, loop);
    console->cpu().state().gpr[2] = 12;
  }
  traced->tracing().configure({.mask = all_traces, .capacity = 8});
  const std::uint64_t target = 10000;
  equal(plain->cpu().run_block(target), true);
  equal(traced->cpu().run_block(target), true);
  equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
  equal(traced->tracing().records().size(), 8);
  equal(traced->tracing().dropped() > 0, true);
}

void cpu_events() {
  for (unsigned scenario = 0; scenario < 7; ++scenario) {
    auto plain = machine(), traced = machine();
    traced->tracing().configure({.mask = all_traces});
    for (auto *console : {plain.get(), traced.get()}) {
      auto &cpu = console->cpu();
      if (scenario == 0) {
        cpu.execute(test::i(4, 0, 0, 3));
        cpu.execute(12);
      } else if (scenario == 1) {
        cpu.write_control(Status, 0x30000401);
        cpu.set_interrupt(2, true);
        cpu.step();
      } else if (scenario == 2) {
        cpu.request_nmi();
        cpu.step();
      } else if (scenario == 3) {
        cpu.set_pc(0x20000000);
        cpu.step();
      } else {
        cpu.write_control(Index, 3);
        cpu.write_control(EntryHi, 0x20000000);
        cpu.write_control(EntryLo0, scenario == 4 ? 0x5f : scenario == 5 ? 0x5b : 0x58);
        cpu.write_control(EntryLo1, 0x9f);
        cpu.execute(test::co(2));
        cpu.state().gpr[1] = 0x20000000;
        cpu.execute(test::i(scenario == 4 ? 35 : 43, 1, 2, 0));
      }
    }
    equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
    const auto records = traced->tracing().records();
    const auto kind = scenario == 1   ? TraceKind::Interrupt
                      : scenario == 2 ? TraceKind::Nmi
                      : scenario == 0 ? TraceKind::Exception
                                      : TraceKind::Tlb;
    const auto found =
        std::find_if(records.begin(), records.end(), [&](const auto &r) { return r.kind == kind; });
    equal(found != records.end(), true);
    if (scenario == 0 && found != records.end())
      equal(found->delay_slot, true);
    if (scenario == 1 && found != records.end())
      equal(found->code, 4);
    if (scenario >= 3) {
      const auto event = scenario == 3   ? TlbEvent::Miss
                         : scenario == 4 ? TlbEvent::Load
                         : scenario == 5 ? TlbEvent::Modification
                                         : TlbEvent::Invalid;
      equal(std::any_of(records.begin(), records.end(),
                        [&](const auto &r) { return r.kind == TraceKind::Tlb && r.tlb == event; }),
            true);
    }
    const auto text = cupid::desktop::trace_dump(records);
    equal(text.empty(), false);
  }
}

void rsp_execution() {
  for (unsigned variant = 0; variant < 6; ++variant) {
    auto plain = machine(), traced = machine();
    for (auto *console : {plain.get(), traced.get()}) {
      auto &rsp = console->signal();
      const std::array words{test::i(9, 0, 1, 2), test::vector(16, 1, 2, 3, variant),
                             test::i(4, 1, variant & 1 ? 1 : 0, 1), test::i(9, 0, 2, 7),
                             std::uint32_t(13)};
      for (unsigned n = 0; n < words.size(); ++n)
        rsp.write_local(0x1000 + n * 4, 4, words[n]);
      rsp.write_io(16, 1);
    }
    traced->tracing().configure({.mask = all_traces});
    plain->signal().advance(1);
    traced->signal().advance(1);
    equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
    const auto records = traced->tracing().records();
    equal(records.empty(), false);
    for (const auto &record : records) {
      equal(record.kind == TraceKind::RspInstruction, true);
      equal(record.instruction,
            traced->signal().read_local(0x1000 | static_cast<std::uint32_t>(record.pc), 4));
    }
    const auto count = records.size();
    traced->tracing().stop();
    plain->signal().write_status(0, 0);
    traced->signal().write_status(0, 0);
    plain->signal().write_io(16, 1);
    traced->signal().write_io(16, 1);
    plain->signal().advance(128);
    traced->signal().advance(128);
    equal(CoreState::capture(*plain) == CoreState::capture(*traced), true);
    equal(traced->tracing().records().size(), count);
  }
  auto interpreted = machine();
  interpreted->signal().write_local(0x1000, 4, test::i(9, 0, 1, 2));
  interpreted->signal().write_local(0x1004, 4, test::vector(55, 0, 0, 0));
  interpreted->signal().write_io(16, 1);
  interpreted->tracing().configure({.mask = all_traces});
  interpreted->signal().elapse(1);
  interpreted->signal().run_interpreted();
  const auto records = interpreted->tracing().records();
  equal(records.size(), 2);
  if (records.size() == 2) {
    equal(records[0].pc, 0);
    equal(records[1].pc, 4);
    equal(records[0].clock, records[1].clock);
  }
}

} // namespace

void execution_tests() {
  cpu_execution();
  cpu_events();
  rsp_execution();
}

} // namespace tracing_test
