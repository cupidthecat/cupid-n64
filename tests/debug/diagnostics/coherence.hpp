#pragma once

#include "../fixture.hpp"

namespace debug_test {

inline void signal_dma_probes() {
  for (bool native : {false, true})
    for (bool cleaned : {false, true}) {
      auto console = machine(141);
      auto &diagnostics = console->diagnostics();
      diagnostics.configure({.profiling = true, .coherence = true});
      auto &cpu = console->cpu();
      cpu.state().gpr[1] = 0xffffffff80002000;
      cpu.state().gpr[2] = 0x55;
      cpu.execute(test::i(40, 1, 2, 1));
      if (cleaned)
        cpu.execute(test::i(47, 1, 0x19, 0));
      diagnostics.clear_log();
      cpu.set_pc(0xffffffff80003000);
      auto &rsp = console->signal();
      rsp.write_io(0, 0);
      rsp.write_io(4, 0x2000);
      rsp.write_io(8, 0);
      rsp.advance_dma(3);
      equal(diagnostics.profile().read(0x341), 8);
      equal(diagnostics.records().empty(), true);
      equal(Inspection::data_byte(*console, DebugProcessor::Rsp, 1).value_or(0),
            cleaned ? 0x55 : 0);
      equal(diagnostics.records().empty(), true);
      const auto load = test::i(36, 0, 3, 1);
      if (native) {
        rsp.write_local(0x1000, 4, load);
        rsp.write_local(0x1004, 4, 13);
        rsp.write_io(16, 1);
        rsp.elapse(128);
        rsp.run();
      } else
        rsp.execute(load);
      const auto records = diagnostics.records();
      equal(records.size(), cleaned ? 0 : 1);
      if (!records.empty()) {
        const auto &warning = records[0];
        equal(static_cast<unsigned>(warning.kind), static_cast<unsigned>(DiagnosticKind::DmemRead));
        equal(warning.address, 1);
        equal(warning.related_address, 0x2001);
        equal(warning.dma.pc, 0xffffffff80003000);
        equal(warning.fill_pc, 0xffffffff80001000);
        equal(warning.write_pc, 0xffffffff80001000);
        equal(static_cast<unsigned>(warning.origin.processor),
              static_cast<unsigned>(DebugProcessor::Rsp));
      }
      diagnostics.clear_log();
      rsp.execute(load);
      equal(diagnostics.records().empty(), true);
    }
}

inline void device_probes() {
  for (bool native : {false, true}) {
    auto console = machine(141);
    console->diagnostics().configure({.devices = diagnostic_bit(DiagnosticDevice::Rsp)});
    auto &rsp = console->signal();
    const auto load = (16u << 26) | (5u << 16) | (6u << 11);
    if (native) {
      rsp.write_local(0x1000, 4, load);
      rsp.write_local(0x1004, 4, 13);
      rsp.write_io(16, 1);
      console->diagnostics().clear_log();
      rsp.elapse(128);
      rsp.run();
    } else
      rsp.execute(load);
    const auto records = console->diagnostics().records();
    equal(records.size(), 1);
    if (!records.empty()) {
      equal(static_cast<unsigned>(records[0].origin.processor),
            static_cast<unsigned>(DebugProcessor::Rsp));
      equal(records[0].origin.pc, 0);
    }
  }
  std::array<std::vector<std::uint8_t>, 2> snapshots;
  for (unsigned enabled = 0; enabled < 2; ++enabled) {
    auto console = machine(141);
    auto &diagnostics = console->diagnostics();
    if (enabled)
      diagnostics.configure({.profiling = true, .coherence = true, .devices = 0xfff});
    const std::array addresses{0x04300000u, 0x04400000u, 0x0450000cu, 0x04600010u,
                               0x04800018u, 0x04700000u, 0x03f00000u, 0x1fc007c0u,
                               0x04100000u, 0x04040014u, 0x10000000u};
    for (auto address : addresses)
      console->read(address, 4);
    const auto records = diagnostics.records();
    for (auto device : {DiagnosticDevice::Mi, DiagnosticDevice::Vi, DiagnosticDevice::Audio,
                        DiagnosticDevice::Pi, DiagnosticDevice::Si, DiagnosticDevice::Ri,
                        DiagnosticDevice::Rdram, DiagnosticDevice::Pif, DiagnosticDevice::Rdp,
                        DiagnosticDevice::Rsp, DiagnosticDevice::Cartridge})
      equal(std::any_of(records.begin(), records.end(),
                        [&](const auto &record) {
                          return record.device == device &&
                                 record.kind == DiagnosticKind::RegisterRead;
                        }),
            enabled != 0);
    console->write(0x04500000, 4, 0x2000);
    console->write(0x04500004, 4, 8);
    console->write(0x04500008, 4, 1);
    console->audio().sample();
    equal(diagnostics.profile().read(0x381), enabled ? 4 : 0);
    console->write(0x04800000, 4, 0x2040);
    console->write(0x04800010, 4, 0x1fc007c0);
    console->run_clocks(14000);
    equal(diagnostics.profile().read(0x361), enabled ? 64 : 0);
    console->write(0x04600000, 4, 0x2080);
    console->write(0x04600004, 4, 0x10000000);
    console->write(0x0460000c, 4, 7);
    equal(diagnostics.profile().read(0x352), enabled ? 8 : 0);
    snapshots[enabled] = CoreState::capture(*console);
  }
  equal(snapshots[0] == snapshots[1], true);
}

inline void merge_byte_probes() {
  for (unsigned opcode : {42u, 44u, 45u, 46u}) {
    const unsigned bytes = opcode == 44 || opcode == 45 ? 8 : 4;
    for (unsigned offset = 0; offset < bytes; ++offset) {
      std::array<std::uint64_t, 2> hits;
      std::array<std::vector<std::uint8_t>, 2> cached;
      for (unsigned native = 0; native < 2; ++native) {
        auto console = machine(141);
        auto &cpu = console->cpu();
        auto &diagnostics = console->diagnostics();
        diagnostics.configure({.profiling = true, .coherence = true});
        cpu.state().gpr[1] = 0xffffffff80002000;
        cpu.execute(test::i(35, 1, 3, 0));
        diagnostics.clear_profiles();
        cpu.state().gpr[2] = 0x8877665544332211;
        const std::array program{test::i(opcode, 1, 2, offset), 0x0000000cu};
        code(*console, program);
        cpu.set_pc(0xffffffff80001000);
        if (native)
          equal(cpu.run_block(1000), true);
        else {
          cpu.step();
          cpu.step();
        }
        hits[native] = diagnostics.profile().read(0x20);
        cached[native] = Inspection::read(*console, MemoryRegionId::DataCache, 0x2000, 16);
        equal(std::any_of(cached[native].begin(), cached[native].end(),
                          [](auto byte) { return byte != 0; }),
              true);
        equal(hits[native] > 0, true);
        for (unsigned lane = 0; lane < 16; ++lane) {
          diagnostics.clear_log();
          console->read(0x2000 + lane, 1);
          equal(diagnostics.records().empty(), cached[native][lane] == 0);
        }
      }
      equal(hits[0], hits[1]);
      equal(cached[0] == cached[1], true);
    }
  }
}

inline void queued_dma_probes() {
  auto console = machine(141);
  auto &diagnostics = console->diagnostics();
  diagnostics.configure({.profiling = true, .coherence = true});
  auto &cpu = console->cpu();
  cpu.state().gpr[1] = 0xffffffff80002000;
  cpu.state().gpr[2] = 0x55;
  cpu.execute(test::i(40, 1, 2, 1));
  cpu.execute(test::i(40, 1, 2, 0x41));
  cpu.set_pc(0xffffffff80003000);
  auto &rsp = console->signal();
  rsp.write_io(0, 0);
  rsp.write_io(4, 0x2000);
  rsp.write_io(8, 0);
  {
    DiagnosticScope scope(&diagnostics, {DebugProcessor::Rsp, 0x100, 0, true});
    rsp.write_io(0, 16);
    rsp.write_io(4, 0x2040);
    rsp.write_io(8, 0);
  }
  rsp.advance_dma(3);
  rsp.advance_dma(3);
  diagnostics.clear_log();
  rsp.execute(test::i(36, 0, 3, 1));
  rsp.execute(test::i(36, 0, 3, 17));
  const auto records = diagnostics.records();
  equal(records.size(), 2);
  if (records.size() == 2) {
    equal(records[0].dma.pc, 0xffffffff80003000);
    equal(static_cast<unsigned>(records[0].dma.processor),
          static_cast<unsigned>(DebugProcessor::Cpu));
    equal(records[1].dma.pc, 0x100);
    equal(static_cast<unsigned>(records[1].dma.processor),
          static_cast<unsigned>(DebugProcessor::Rsp));
  }
  diagnostics.clear_log();
  for (unsigned n = 0; n < 2; ++n) {
    cpu.set_pc(0xffffffff80004000 + n * 4);
    console->write(0x04500000, 4, 0x2000 + n * 0x40);
    console->write(0x04500004, 4, 8);
  }
  console->write(0x04500008, 4, 1);
  for (unsigned n = 0; n < 3; ++n)
    console->audio().sample();
  const auto audio = diagnostics.records();
  equal(audio.size(), 2);
  if (audio.size() == 2) {
    equal(audio[0].dma.pc, 0xffffffff80004000);
    equal(audio[1].dma.pc, 0xffffffff80004004);
  }
  cpu.state().gpr[1] = 0xffffffff80002080;
  cpu.execute(test::i(35, 1, 2, 0));
  diagnostics.clear_log();
  console->write(0x2080, 1, 0);
  equal(diagnostics.records().size(), 1);
  if (!diagnostics.records().empty())
    equal(diagnostics.records()[0].fill_pc, 0xffffffff80004004);
}

inline void display_traffic_probes() {
  auto console = machine(141);
  auto &diagnostics = console->diagnostics();
  diagnostics.configure({.profiling = true});
  console->write(0x04400000, 4, 2);
  console->write(0x04400004, 4, 0x2000);
  console->write(0x04400008, 4, 32);
  console->write(0x04400018, 4, 525);
  console->write(0x0440001c, 4, 3093);
  console->write(0x04400020, 4, 0x0c150c15);
  console->write(0x04400024, 4, (108u << 16) | 140);
  console->write(0x04400028, 4, (34u << 16) | 36);
  console->write(0x04400030, 4, 1024);
  console->write(0x04400034, 4, 1024);
  console->video().scanout(console->ram());
  equal(diagnostics.profile().read(0x391), 68);
  raw_word(*console, 0x3000, 0x29000000);
  raw_word(*console, 0x3004, 0);
  console->write(0x04100000, 4, 0x3000);
  console->write(0x04100004, 4, 0x3008);
  equal(diagnostics.profile().read(0x3a1), 8);
  equal(diagnostics.dma_origin(MemoryRequestor::DisplayDma).pc, 0xffffffff80001000);
  diagnostics.set_homebrew(true);
  console->write(0x04700010, 4, 0);
  console->run_clocks(3200000);
  const auto records = diagnostics.records();
  equal(std::count_if(records.begin(), records.end(),
                      [](const auto &record) {
                        return record.kind == DiagnosticKind::RefreshDisabled &&
                               record.address == 0x04700010;
                      }),
        1);
}

inline void serial_dma_probes() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    const bool reverse = scenario & 1;
    const bool restored = scenario & 2;
    std::array<std::vector<std::uint8_t>, 2> states;
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
      auto console = machine(141);
      auto &cpu = console->cpu();
      auto &diagnostics = console->diagnostics();
      diagnostics.configure({.coherence = enabled != 0});
      cpu.state().gpr[1] = 0xffffffff80002000;
      cpu.state().gpr[2] = 0x55;
      cpu.execute(test::i(40, 1, 2, 1));
      console->write(0x04800000, 4, 0x2000);
      const auto budget = std::uint64_t(console->pif().estimate_timing()) * 3 + 4096;
      for (unsigned n = 0; n < 2; ++n) {
        const bool read = (n == 0) != reverse;
        cpu.set_pc(read ? 0xffffffff80003000 : 0xffffffff80004000);
        console->write(read ? 0x04800004 : 0x04800010, 4, 0x1fc007c0);
      }
      if (restored)
        CoreState::restore(*console, CoreState::capture(*console));
      diagnostics.clear_log();
      console->run_clocks(budget);
      const auto records = diagnostics.records();
      bool read = false, write = false;
      for (const auto &record : records) {
        if (record.requestor != MemoryRequestor::SerialDma)
          continue;
        const bool from_pif = record.kind == DiagnosticKind::CachedWrite;
        read |= from_pif;
        write |= record.kind == DiagnosticKind::DirtyRead;
        equal(record.dma.known, !restored);
        equal(record.dma.pc, restored ? 0 : from_pif ? 0xffffffff80003000 : 0xffffffff80004000);
      }
      equal(read, enabled != 0);
      equal(write, enabled != 0);
      states[enabled] = CoreState::capture(*console);
    }
    equal(states[0] == states[1], true);
  }
}

} // namespace debug_test
