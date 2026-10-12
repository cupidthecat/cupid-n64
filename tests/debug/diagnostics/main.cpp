#include "../fixture.hpp"
#include "boundaries.hpp"
#include "coherence.hpp"
#include "core/debug/diagnostics.hpp"
#include "homebrew.hpp"
#include "native.hpp"
#include "protocol.hpp"
#include <stdexcept>

namespace {

using namespace debug_test;

void log_tests() {
  auto console = machine(141);
  auto &diagnostics = console->diagnostics();
  diagnostics.register_access(DiagnosticDevice::Mi, 0x04300000, 1, false);
  equal(diagnostics.records().empty(), true);
  DiagnosticConfig config;
  config.devices = diagnostic_bit(DiagnosticDevice::Mi);
  config.capacity = 3;
  config.first_address = 0x04300000;
  config.last_address = 0x0430000c;
  diagnostics.configure(config);
  for (unsigned n = 0; n < 1000; ++n) {
    diagnostics.register_access(DiagnosticDevice::Vi, 0x04300000, n, false);
    diagnostics.register_access(DiagnosticDevice::Mi, 0x04300010, n, false);
    diagnostics.register_access(DiagnosticDevice::Mi, 0x04300000, n, n & 1);
  }
  const auto before = diagnostics.records();
  equal(before.size(), 3);
  equal(diagnostics.dropped(), 997);
  for (unsigned n = 0; n < before.size(); ++n) {
    equal(before[n].sequence, 998 + n);
    equal(before[n].value, 997 + n);
  }
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    auto invalid = config;
    if (scenario == 0)
      invalid.capacity = 0;
    if (scenario == 1)
      invalid.capacity = Diagnostics::maximum_records + 1;
    if (scenario == 2)
      invalid.first_address = invalid.last_address + 1;
    if (scenario == 3)
      invalid.cpu = {2, 1};
    if (scenario == 4)
      invalid.rsp.last = 4096;
    if (scenario == 5)
      invalid.devices = 0x80000000;
    bool rejected = false;
    try {
      diagnostics.configure(invalid);
    } catch (const std::exception &) {
      rejected = true;
    }
    equal(rejected, true);
    equal(diagnostics.records().size(), 3);
    equal(diagnostics.records()[0].sequence, before[0].sequence);
  }
  diagnostics.clear_log();
  equal(diagnostics.records().empty(), true);
}

void profile_tests() {
  auto console = machine(141);
  auto &diagnostics = console->diagnostics();
  diagnostics.cpu_cycles(8, true);
  equal(diagnostics.profile().read(0), 0);
  diagnostics.configure({.profiling = true});
  diagnostics.cpu_cycles(10, false);
  diagnostics.cpu_cycles(4, true);
  diagnostics.rsp_cycles(12, false);
  diagnostics.rsp_cycles(128, true);
  diagnostics.cache_access(true, true);
  diagnostics.cache_access(true, false);
  diagnostics.cache_writeback(true);
  diagnostics.cache_access(false, true);
  diagnostics.cache_access(false, false);
  diagnostics.cache_writeback(false);
  equal(diagnostics.profile().read(0), 14);
  equal(diagnostics.profile().read(1), 4);
  equal(diagnostics.profile().read(0x200), 140);
  equal(diagnostics.profile().read(0x201), 128);
  for (unsigned code : {0x10u, 0x11u, 0x12u, 0x20u, 0x21u, 0x22u})
    equal(diagnostics.profile().read(code), 1);
  unsigned reads = 0, writes = 0;
  for (unsigned n = 0; n < static_cast<unsigned>(MemoryRequestor::Count); ++n) {
    diagnostics.traffic(static_cast<MemoryRequestor>(n), n + 1, false);
    diagnostics.traffic(static_cast<MemoryRequestor>(n), n + 2, true);
    equal(diagnostics.profile().read(0x311 + n * 16), n + 1);
    equal(diagnostics.profile().read(0x312 + n * 16), n + 2);
    equal(diagnostics.profile().read(0x310 + n * 16), 2 * n + 3);
    reads += n + 1;
    writes += n + 2;
  }
  diagnostics.traffic(MemoryRequestor::Host, 1000, false);
  equal(diagnostics.profile().read(0x300), reads + writes);
  equal(diagnostics.profile().read(0x301), reads);
  equal(diagnostics.profile().read(0x302), writes);
  for (unsigned code : {2u, 0x13u, 0x23u, 0x202u, 0x303u, 0x3b0u, 0x1300u})
    equal(diagnostics.profile().read(code), 0);
}

void guest_probes() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    std::array<std::vector<std::uint8_t>, 2> states;
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
      auto console = machine(141);
      const std::array program{test::i(35, 1, 2, 0), test::i(43, 1, 2, 4), 0x0000000cu};
      code(*console, program);
      raw_word(*console, 0x2000, 0x12345678);
      console->cpu().state().gpr[1] = 0xffffffff80002000;
      console->diagnostics().configure({.profiling = enabled != 0});
      if (mode == 0)
        for (unsigned n = 0; n < program.size(); ++n)
          console->cpu().step();
      if (mode == 1)
        equal(console->cpu().run_interpreted_block(1000), true);
      if (mode == 2)
        equal(console->cpu().run_block(1000), true);
      console->synchronize();
      states[enabled] = CoreState::capture(*console);
      const auto &profile = console->diagnostics().profile();
      equal(profile.read(0) > 0, enabled != 0);
      equal(profile.read(0x11), enabled ? 1 : 0);
      equal(profile.read(0x21), enabled ? 1 : 0);
      equal(profile.read(0x20), enabled ? 1 : 0);
      equal(profile.read(0x311), enabled ? 32 : 0);
      equal(profile.read(0x321), enabled ? 16 : 0);
      equal(profile.read(0x200) > 0, enabled != 0);
    }
    equal(states[0] == states[1], true);
  }
  auto console = machine(141);
  auto &diagnostics = console->diagnostics();
  diagnostics.configure({.coherence = true, .devices = diagnostic_bit(DiagnosticDevice::Mi)});
  console->cpu().state().gpr[1] = 0xffffffff80002000;
  console->cpu().state().gpr[2] = 0x7f;
  console->cpu().execute(test::i(40, 1, 2, 1));
  diagnostics.clear_log();
  console->read(0x2002, 1);
  equal(diagnostics.records().empty(), true);
  console->read(0x2001, 1);
  equal(diagnostics.records().size(), 1);
  if (!diagnostics.records().empty()) {
    equal(static_cast<unsigned>(diagnostics.records()[0].kind),
          static_cast<unsigned>(DiagnosticKind::DirtyRead));
    equal(diagnostics.records()[0].address, 0x2001);
    equal(diagnostics.records()[0].write_pc, 0xffffffff80001000);
  }
  console->cpu().execute(test::i(47, 1, 0x19, 0));
  diagnostics.clear_log();
  console->read(0x2001, 1);
  equal(diagnostics.records().empty(), true);
  console->write(0x2000, 1, 0);
  equal(diagnostics.records().size(), 1);
  console->cpu().execute(test::i(47, 1, 0x11, 0));
  diagnostics.clear_log();
  console->write(0x2000, 1, 1);
  equal(diagnostics.records().empty(), true);
  console->read(0x04300000, 4);
  equal(diagnostics.records().size(), 1);
}

} // namespace

int main() {
  log_tests();
  profile_tests();
  guest_probes();
  homebrew_probes();
  signal_dma_probes();
  device_probes();
  merge_byte_probes();
  queued_dma_probes();
  display_traffic_probes();
  profile_command_probes();
  exception_probes();
  signal_command_boundaries();
  homebrew_memory_probes();
  format_probes();
  counter_boundary_probes();
  signal_boundary_probes();
  delay_profile_probes();
  refill_profile_probes();
  serial_dma_probes();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
