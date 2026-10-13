#pragma once

#include "../../cpu/native_memory_fixture.hpp"
#include "../fixture.hpp"

namespace debug_test {

inline void delay_profile_probes() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    test::native_memory::CachedFixture fixture, expected;
    auto host = machine(130);
    auto &diagnostics = host->diagnostics();
    diagnostics.connect(*host, fixture.cpu, host->tracing());
    fixture.cpu.connect_diagnostics(&diagnostics);
    for (auto *sample : {&fixture, &expected}) {
      sample->cpu.state().gpr[1] = 0xffffffff80002000;
      sample->code(0, 0x10000002);
      sample->code(4, test::i(55, 1, 2, 1));
    }
    diagnostics.configure({.profiling = true});
    const auto start = fixture.cpu.state().clocks;
    for (auto *sample : {&fixture, &expected}) {
      if (mode == 0) {
        sample->cpu.step();
        sample->cpu.step();
      } else if (mode == 1)
        equal(sample->cpu.run_interpreted_block(1000), true);
      else
        equal(sample->cpu.run_block(1000), true);
    }
    const auto clocks = fixture.cpu.state().clocks - start;
    equal(clocks, mode == 2 ? 100 : 102);
    equal(diagnostics.profile().cpu[0], clocks >> 1);
    equal(diagnostics.profile().cpu[1], 0);
    equal(fixture.cpu.read_control(Cause) & 0x8000007c, 0x80000010);
    test::native_memory::compare(fixture, expected);
  }
}

struct DiagnosticRefillMemory : test::native_memory::CachedMemory {
  bool wrote = false;
  bool fail_fill = false;

  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    if (wrote && fail_fill)
      return {0, false};
    return test::native_memory::CachedMemory::read_burst(address, output);
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    wrote = true;
    words[(address >> 2) % words.size()] = static_cast<std::uint32_t>(value);
    return Memory::write(address, bytes, value);
  }
};

inline void refill_profile_probes() {
  for (bool fault : {false, true}) {
    DiagnosticRefillMemory memory;
    Cpu cpu(memory);
    auto host = machine(130);
    auto &diagnostics = host->diagnostics();
    diagnostics.connect(*host, cpu, host->tracing());
    cpu.connect_diagnostics(&diagnostics);
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, 0x7006e460);
    cpu.state().gpr[1] = 0xffffffffa0001020;
    cpu.state().gpr[2] = 12;
    memory.words[0x101c / 4] = test::i(43, 1, 2, 0);
    memory.words[0x1020 / 4] = test::i(9, 0, 4, 7);
    memory.words[0x1024 / 4] = 12;
    cpu.set_pc(0xffffffff80001000);
    cpu.step();
    cpu.set_pc(0xffffffff8000101c);
    memory.fail_fill = fault;
    diagnostics.configure({.profiling = true});
    const auto start = cpu.state().clocks;
    equal(cpu.run_block(1000), true);
    equal(cpu.state().pc, 0xffffffff80000180);
    equal(cpu.read_control(Epc), 0xffffffff80001020);
    equal(cpu.read_control(Cause) & 0x7c, fault ? 0x18 : 0x20);
    const auto clocks = cpu.state().clocks - start;
    equal(diagnostics.profile().cpu[0], clocks >> 1);
    equal(diagnostics.profile().cpu[1], fault ? 1 : 0);
  }
}

} // namespace debug_test
