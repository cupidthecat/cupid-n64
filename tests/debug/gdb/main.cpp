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
      equal(debugger.stop().address, debugger.canonical(watched));
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

void map_pages(Cpu &cpu, std::uint64_t address, std::uint32_t first, std::uint32_t second) {
  cpu.write_control(Index, 0);
  cpu.write_control(PageMask, 0);
  cpu.write_control(EntryHi, address);
  cpu.write_control(EntryLo0, (first >> 6) | 0x1f);
  cpu.write_control(EntryLo1, (second >> 6) | 0x1f);
  cpu.execute(test::co(2));
}

void extended_address_tests() {
  auto console = machine(131);
  auto &cpu = console->cpu();
  cpu.write_control(Status, 0x30000080);
  map_pages(cpu, 0x80000000, 0x4000, 0x2000);
  raw_word(*console, 0x1000, 0x11223344);
  raw_word(*console, 0x2000, 0xaabbccdd);
  Debugger debugger(*console);
  debugger.attach();
  const auto before = CoreState::capture(*console);
  equal(debugger.read_memory(0x0000000080001000, 4).value_or(std::vector<std::uint8_t>{}) ==
            std::vector<std::uint8_t>({0xaa, 0xbb, 0xcc, 0xdd}),
        true);
  equal(debugger.read_memory(0xffffffff80001000, 4).value_or(std::vector<std::uint8_t>{}) ==
            std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x44}),
        true);
  equal(CoreState::capture(*console) == before, true);
  equal(debugger.write_register(37, 0x0000000080001000), true);
  equal(cpu.state().pc, 0x0000000080001000);
  const std::array<std::uint8_t, 1> replacement{0x55};
  equal(debugger.write_memory(0x0000000080001001, replacement), true);
  equal(Inspection::read(*console, MemoryRegionId::Rdram, 0x2001, 1).front(), 0x55);
  equal(Inspection::read(*console, MemoryRegionId::Rdram, 0x1001, 1).front(), 0x22);
  raw_word(*console, 0x2000, test::i(9, 7, 7, 1));
  raw_word(*console, 0x2004, test::i(9, 7, 7, 1));
  equal(debugger.point(DebugPointKind::Software, 0x0000000080001004, 4, true), true);
  debugger.resume(false, 0x0000000080001000);
  debugger.advance(1000);
  equal(static_cast<unsigned>(debugger.stop().kind),
        static_cast<unsigned>(DebugStopKind::Breakpoint));
  equal(cpu.state().pc, 0x0000000080001004);
  equal(cpu.state().gpr[7], 1);
  for (auto status :
       {0x30000000u, 0x30000080u, 0x30000008u, 0x30000048u, 0x30000010u, 0x30000030u}) {
    cpu.write_control(Status, status);
    equal(debugger.canonical(0x0000000080001000),
          (status & 0xe0) ? 0x0000000080001000 : 0xffffffff80001000);
  }
}

void mapped_watch_tests() {
  for (auto kind : {DebugPointKind::Read, DebugPointKind::Write, DebugPointKind::Access})
    for (bool cached : {false, true})
      for (auto physical : {0x2ffcu, 0x5000u, 0x3000u}) {
        auto console = machine(131);
        auto &cpu = console->cpu();
        map_pages(cpu, 0x10000, 0x2000, 0x5000);
        cpu.set_pc(0xffffffff80001000);
        cpu.state().gpr[1] = (cached ? 0xffffffff80000000 : 0xffffffffa0000000) | physical;
        const std::array program{test::i(kind == DebugPointKind::Write ? 43 : 35, 1, 2, 0), 12u};
        code(*console, program);
        Debugger debugger(*console);
        debugger.attach();
        equal(debugger.point(kind, 0x10ffe, 8, true), true);
        debugger.resume(true);
        debugger.advance(1000);
        const auto stop = physical == 0x3000              ? DebugStopKind::Step
                          : kind == DebugPointKind::Read  ? DebugStopKind::ReadWatch
                          : kind == DebugPointKind::Write ? DebugStopKind::WriteWatch
                                                          : DebugStopKind::AccessWatch;
        equal(static_cast<unsigned>(debugger.stop().kind), static_cast<unsigned>(stop));
        equal(cpu.state().pc, 0xffffffff80001004);
      }
}

void display_peek_tests() {
  auto console = machine(131);
  auto &rdp = console->display();
  auto &command = const_cast<DisplayCommandState &>(rdp.command());
  command.start = 0x1000;
  command.end = 0x2000;
  command.current = 0x1800;
  command.clock = 123;
  command.buffer_busy = 5;
  command.pipe_busy = 9;
  command.tmem_busy = 11;
  unsigned synchronizations = 0;
  rdp.connect_sync([&] { ++synchronizations; });
  Debugger debugger(*console);
  debugger.attach();
  const auto before = CoreState::capture(*console);
  for (unsigned index = 0; index < 8; ++index) {
    const auto value = rdp.peek_word(index * 4);
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
    equal(debugger.read_memory(0xa4100000 + index * 4, 4).value_or(std::vector<std::uint8_t>{}) ==
              std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
          true);
    equal(synchronizations, 0);
  }
  equal(CoreState::capture(*console) == before, true);
  equal(rdp.read_word(16), 123);
  equal(synchronizations, 2);
  command.buffer_busy = 7;
  equal(rdp.peek_word(20), 11);
  rdp.connect_sync([&] {
    command.tmem_busy += 3;
    ++synchronizations;
  });
  equal(rdp.read_word(20), 14);
  equal(command.tmem_busy, 17);
  equal(synchronizations, 4);
}
} // namespace

int main() {
  register_tests();
  execution_tests();
  watch_tests();
  memory_tests();
  extended_address_tests();
  mapped_watch_tests();
  display_peek_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
