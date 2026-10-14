#pragma once

#include "core/debug/disassembly.hpp"
#include "desktop/debug/format.hpp"
#include "homebrew.hpp"

namespace debug_test {

inline void profile_command_probes() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    auto guest = machine(130);
    guest->diagnostics().set_homebrew(true);
    auto &processor = guest->cpu();
    processor.state().gpr[4] = 0xffffffff80002000;
    processor.execute(test::i(35, 4, 5, 0));
    processor.state().gpr[1] = 3;
    processor.state().gpr[2] = 0x20;
    const std::array program{emux(0x28, 1, 2, 1), test::i(35, 4, 5, 0), emux(0x28, 1, 2, 2),
                             emux(0x29), 12u};
    code(*guest, program);
    processor.set_pc(0xffffffff80001000);
    if (mode == 0)
      for (unsigned n = 0; n < program.size(); ++n)
        processor.step();
    else if (mode == 1)
      equal(processor.run_interpreted_block(1000), true);
    else
      equal(processor.run_block(1000), true);
    equal(processor.state().gpr[2], 1);
  }
  auto console = machine(130);
  auto &diagnostics = console->diagnostics();
  auto &cpu = console->cpu();
  diagnostics.set_homebrew(true);
  cpu.state().gpr[4] = 0xffffffff80002000;
  cpu.execute(test::i(35, 4, 5, 0));
  cpu.state().gpr[1] = 3;
  cpu.execute(emux(0x28, 1, 2, 1));
  cpu.execute(test::i(35, 4, 5, 0));
  cpu.execute(test::i(35, 4, 5, 4));
  cpu.execute(emux(0x28, 1, 2, 2));
  cpu.state().gpr[2] = 0x20;
  cpu.execute(emux(0x29));
  equal(cpu.state().gpr[2], 2);
  cpu.execute(emux(0x28, 1, 2, 3));
  cpu.state().gpr[2] = 0x20;
  cpu.execute(emux(0x29));
  equal(cpu.state().gpr[2], 0);
  cpu.state().gpr[1] = 1024;
  cpu.execute(emux(0x28, 1, 2, 1));
  cpu.state().gpr[2] = 0x20;
  cpu.execute(emux(0x29));
  equal(cpu.state().gpr[2], 0);
  cpu.state().gpr[1] = ~0ull;
  cpu.state().gpr[2] = 0x100000020ull;
  cpu.execute(emux(0x29));
  equal(cpu.state().gpr[2], 0);
  for (bool native : {false, true}) {
    auto signal = machine(130);
    signal->diagnostics().set_homebrew(true);
    auto &rsp = signal->signal();
    rsp.state().gpr[1] = 3;
    rsp.state().gpr[2] = 0x200;
    const std::array program{emux(0x28, 1, 2, 1), test::i(9, 0, 6, 42), emux(0x28, 1, 2, 2),
                             emux(0x29), 13u};
    for (unsigned n = 0; n < program.size(); ++n)
      rsp.write_local(0x1000 + n * 4, 4, program[n]);
    rsp.write_io(16, 1);
    rsp.elapse(128);
    if (native)
      rsp.run();
    else
      rsp.run_interpreted();
    equal(rsp.state().gpr[2], 6);
    equal(rsp.state().gpr[6], 42);
    rsp.state().gpr[1] = ~0u;
    rsp.state().gpr[2] = 0x200;
    const auto total = signal->diagnostics().profile().rsp[0];
    rsp.execute(emux(0x29));
    equal(rsp.state().gpr[2], static_cast<std::uint32_t>(total));
    rsp.execute(emux(0x28, 1, 2, 4));
    rsp.state().gpr[1] = 3;
    rsp.state().gpr[2] = 0x200;
    rsp.execute(emux(0x29));
    equal(rsp.state().gpr[2], 0);
  }
}

inline void exception_probes() {
  for (unsigned mode = 0; mode < 3; ++mode)
    for (bool enabled : {false, true})
      homebrew_cpu(mode, enabled, emux(0x2a), 0, 1u << 8, [&](Console &console) {
        const auto records = console.diagnostics().records();
        equal(records.size(), 0);
        equal((console.cpu().read_control(Cause) >> 2) & 31, 8);
        equal(console.cpu().read_control(Epc), 0xffffffff80001004);
      });
  auto console = machine(130);
  auto &cpu = console->cpu();
  console->diagnostics().set_homebrew(true);
  cpu.state().gpr[2] = 1u << 9;
  cpu.execute(emux(0x2a));
  auto &rsp = console->signal();
  rsp.write_io(16, 0x100);
  rsp.execute(13);
  const auto records = console->diagnostics().records();
  equal(records.size(), 0);
  equal(rsp.status().halted && rsp.status().broken, true);
  equal(console->interrupts().read_word(8) & 1, 1);
}

inline void signal_command_boundaries() {
  for (bool native : {false, true}) {
    std::array<std::vector<std::uint8_t>, 2> states;
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
      auto console = machine(130);
      auto &rsp = console->signal();
      auto &diagnostics = console->diagnostics();
      diagnostics.set_homebrew(enabled != 0);
      rsp.state().gpr[1] = ~0u;
      rsp.state().gpr[2] = 1u << 9;
      rsp.state().vectors[4].lanes[3] = 0x8000;
      const std::array program{emux(0x26, 1, 2, 0), emux(0x26, 1, 2, 18), emux(0x26, 1, 2, 23),
                               emux(0x2a), 13u};
      for (unsigned n = 0; n < program.size(); ++n)
        rsp.write_local(0x1000 + n * 4, 4, program[n]);
      rsp.write_io(16, 0x101);
      rsp.elapse(128);
      if (native)
        rsp.run();
      else
        rsp.run_interpreted();
      equal(rsp.status().semaphore, false);
      const auto records = diagnostics.records();
      equal(records.size(), enabled ? 3 : 0);
      if (!records.empty()) {
        equal(records[0].text.find("COP0 7 = 00000000") != std::string::npos, true);
        equal(records[1].text.find("V4 =") != std::string::npos, true);
        equal(records[2].text.find("R1 = -1") != std::string::npos, true);
      }
      states[enabled] = CoreState::capture(*console);
    }
    equal(states[0] == states[1], true);
    for (unsigned control = 1; control < 4; ++control) {
      auto console = machine(130);
      console->diagnostics().set_homebrew(true);
      auto &rsp = console->signal();
      rsp.write_local(0x1000, 4, emux(0x2c, 1, 2, control));
      rsp.write_local(0x1004, 4, 13);
      rsp.write_io(16, 1);
      rsp.elapse(128);
      if (native)
        rsp.run();
      else
        rsp.run_interpreted();
      equal(static_cast<unsigned>(console->diagnostics().take_control()), control);
    }
  }
  auto console = machine(130);
  auto &diagnostics = console->diagnostics();
  for (bool manual : {false, true}) {
    console->tracing().set_enabled(TraceKind::CpuInstruction, true);
    console->tracing().set_enabled(TraceKind::RspInstruction, manual);
    diagnostics.set_homebrew(true);
    console->signal().execute(emux(0x23));
    equal(console->tracing().enabled(TraceKind::RspInstruction), true);
    diagnostics.stop();
    equal(console->tracing().enabled(TraceKind::RspInstruction), manual);
    equal(console->tracing().enabled(TraceKind::CpuInstruction), true);
  }
}

inline void homebrew_memory_probes() {
  auto console = machine(130);
  auto &cpu = console->cpu();
  auto &diagnostics = console->diagnostics();
  diagnostics.set_homebrew(true);
  unsigned fenced = 0;
  diagnostics.connect_memory_fence([&] { ++fenced; });
  const std::vector<std::uint8_t> message(Diagnostics::maximum_text + 16, 'a');
  Inspection::write(*console, MemoryRegionId::Rdram, 0x2000,
                    std::span(message).first(Diagnostics::maximum_text));
  cpu.state().gpr[1] = 0xffffffff80002000;
  cpu.state().gpr[2] = ~0ull;
  cpu.execute(emux(0x25, 1, 2, 1));
  equal(diagnostics.records().back().text.size(), Diagnostics::maximum_text);
  cpu.execute(emux(0x27));
  equal(diagnostics.records().back().text.size() <= Diagnostics::maximum_text, true);
  equal(fenced, 2);
  cpu.state().gpr[4] = 0xffffffff80002000;
  cpu.state().gpr[5] = 'Z';
  cpu.execute(test::i(40, 4, 5, 1));
  cpu.state().gpr[1] = 0xffffffff80002001;
  cpu.state().gpr[2] = 1;
  cpu.execute(emux(0x25, 1, 2, 1));
  equal(diagnostics.records().back().text == "Z", true);
  const auto before = CoreState::capture(*console);
  diagnostics.set_homebrew(false);
  cpu.execute(emux(0x25));
  equal(fenced, 3);
  CoreState::restore(*console, before);
  equal(diagnostics.homebrew(), false);
  diagnostics.configure({.coherence = true});
  console->read(0x2001, 1);
  equal(diagnostics.records().size(), 1);
}

inline void format_probes() {
  for (auto processor : {DebugProcessor::Cpu, DebugProcessor::Rsp})
    for (unsigned function = 32; function < 64; ++function) {
      const auto word = emux(function, 7, 9, 27);
      const auto command = HomebrewCommand::decode(word);
      if (!command.name(processor).empty())
        equal(disassemble(processor, 0, word).find(command.name(processor)) == 0, true);
    }
  std::vector<DiagnosticRecord> records(1000);
  for (unsigned n = 0; n < records.size(); ++n) {
    records[n].kind = DiagnosticKind::HomebrewLog;
    records[n].sequence = n + 1;
    records[n].text = std::string(4096, 'a');
  }
  records.back().text = std::string("last\0", 5);
  const auto output = cupid::desktop::diagnostic_dump(records);
  equal(output.size() <= 65536, true);
  equal(output.find("1000 ") != std::string::npos, true);
  equal(output.find("last\\x00") != std::string::npos, true);
  equal(output.find('\0') == std::string::npos, true);
  ProfileSnapshot profile;
  profile.traffic[3].reads = 123;
  equal(cupid::desktop::profile_dump(profile).find("SP DMA: 123 read") != std::string::npos, true);
}

} // namespace debug_test
