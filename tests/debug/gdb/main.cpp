#include "../fixture.hpp"
#include "core/debug/debugger.hpp"

namespace {
using namespace debug_test;

void register_tests() {
  auto console = machine(131);
  auto &cpu = console->cpu();
  cpu.state().gpr[7] = 0x123456789abcdef0;
  cpu.state().fpr[31] = 0x1122334455667788;
  const auto before = CoreState::capture(*console);
  Debugger debugger(*console);
  equal(debugger.read_register(7).has_value(), false);
  debugger.attach();
  equal(debugger.attached(), true);
  equal(debugger.halted(), true);
  equal(debugger.read_register(0).value_or(1), 0);
  equal(debugger.read_register(7).value_or(0), 0x123456789abcdef0);
  equal(debugger.read_register(37).value_or(0), 0xffffffff80001000);
  equal(debugger.read_register(69).value_or(0), 0x1122334455667788);
  equal(debugger.read_register(71).has_value(), false);
  equal(debugger.write_register(0, ~0ull), true);
  equal(cpu.state().gpr[0], 0);
  equal(debugger.write_register(32, ~0ull), true);
  equal(cpu.read_control(Status), 0x30000000);
  equal(debugger.write_register(37, 0x80001001), false);
  const std::array addresses{0xa404001cu, 0xa4300000u, 0xa4400000u, 0xa450000cu,
                             0xa4600010u, 0xa4700000u, 0xa4800018u};
  for (auto address : addresses)
    equal(debugger.read_memory(address, 4).has_value(), true);
  equal(CoreState::capture(*console) == before, true);
  equal(debugger.write_register(7, 42), true);
  equal(cpu.state().gpr[7], 42);
  equal(debugger.write_register(37, 0x80002000), true);
  equal(cpu.state().pc, 0xffffffff80002000);
  const auto edited = CoreState::capture(*console);
  debugger.detach();
  equal(CoreState::capture(*console) == edited, true);
}

void execution_tests() {
  auto actual = machine(131), expected = machine(131);
  const std::array program{test::i(9, 7, 7, 1), test::i(9, 7, 7, 1), 12u};
  code(*actual, program);
  code(*expected, program);
  Debugger debugger(*actual);
  debugger.attach();
  equal(debugger.point(DebugPointKind::Software, 0x80001004, 4, true), true);
  debugger.resume();
  debugger.advance(1000);
  expected->step();
  equal(debugger.halted(), true);
  equal(static_cast<unsigned>(debugger.stop().kind),
        static_cast<unsigned>(DebugStopKind::Breakpoint));
  equal(actual->cpu().state().gpr[7], 1);
  equal(actual->cpu().state().pc, 0xffffffff80001004);
  equal(CoreState::capture(*actual) == CoreState::capture(*expected), true);
  const auto stopped = CoreState::capture(*actual);
  debugger.advance(1000);
  equal(CoreState::capture(*actual) == stopped, true);
  debugger.resume(true);
  debugger.advance(1000);
  expected->step();
  equal(static_cast<unsigned>(debugger.stop().kind), static_cast<unsigned>(DebugStopKind::Step));
  equal(actual->cpu().state().gpr[7], 2);
  equal(CoreState::capture(*actual) == CoreState::capture(*expected), true);
  debugger.detach();
  actual->run_interval(1000);
  expected->run_interval(1000);
  equal(CoreState::capture(*actual) == CoreState::capture(*expected), true);
}

void watch_tests() {
  for (auto kind : {DebugPointKind::Read, DebugPointKind::Write, DebugPointKind::Access})
    for (bool cached : {false, true}) {
      auto console = machine(131);
      raw_word(*console, 0x2000, 0x12345678);
      console->cpu().state().gpr[1] = cached ? 0xffffffff80002000 : 0xffffffffa0002000;
      const std::array program{test::i(35, 1, 2, 0), test::i(43, 1, 2, 4), 12u};
      code(*console, program);
      Debugger debugger(*console);
      debugger.attach();
      const auto watched = kind == DebugPointKind::Write ? 0xa0002004u : 0x80002000u;
      equal(debugger.point(kind, watched, 4, true), true);
      debugger.resume();
      debugger.advance(1000);
      equal(debugger.halted(), true);
      const auto stop = kind == DebugPointKind::Read    ? DebugStopKind::ReadWatch
                        : kind == DebugPointKind::Write ? DebugStopKind::WriteWatch
                                                        : DebugStopKind::AccessWatch;
      equal(static_cast<unsigned>(debugger.stop().kind), static_cast<unsigned>(stop));
      equal(debugger.stop().address, Debugger::canonical(watched));
      equal(console->cpu().state().pc,
            kind == DebugPointKind::Write ? 0xffffffff80001008 : 0xffffffff80001004);
      equal(console->cpu().state().gpr[2], 0x12345678);
    }
}

void memory_tests() {
  auto console = machine(131);
  auto &cpu = console->cpu();
  raw_word(*console, 0x2000, 0x11223344);
  cpu.write_control(Index, 0);
  cpu.write_control(PageMask, 0);
  cpu.write_control(EntryHi, 0x40000);
  cpu.write_control(EntryLo0, (0x2000 >> 6) | 0x1b);
  cpu.write_control(EntryLo1, 0);
  cpu.execute(test::co(2));
  cpu.state().gpr[1] = 0x40000;
  cpu.execute(test::i(35, 1, 2, 0));
  console->synchronize();
  Debugger debugger(*console);
  debugger.attach();
  equal(debugger.read_memory(0x40000, 4).value_or(std::vector<std::uint8_t>{}) ==
            std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x44}),
        true);
  const auto before = CoreState::capture(*console);
  const std::array<std::uint8_t, 1> replacement{0xaa};
  equal(debugger.write_memory(0x40001, replacement), false);
  equal(debugger.write_memory(0x41000, replacement), false);
  equal(debugger.write_memory(0xb0000000, replacement), false);
  equal(CoreState::capture(*console) == before, true);
  cpu.write_control(EntryLo0, (0x2000 >> 6) | 0x1f);
  cpu.execute(test::co(2));
  const auto clocks = cpu.state().clocks;
  equal(debugger.write_memory(0x40001, replacement), true);
  equal(cpu.state().clocks, clocks);
  equal(debugger.read_memory(0x40000, 4).value_or(std::vector<std::uint8_t>{}) ==
            std::vector<std::uint8_t>({0x11, 0xaa, 0x33, 0x44}),
        true);
  equal(debugger.read_memory(0xa0002000, 4).value_or(std::vector<std::uint8_t>{}) ==
            std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x44}),
        true);
  equal(debugger.read_memory(0x41000, 4).has_value(), false);
  equal(debugger.read_memory(0x80002000, Debugger::maximum_transfer + 1).has_value(), false);
}
} // namespace

int main() {
  register_tests();
  execution_tests();
  watch_tests();
  memory_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
