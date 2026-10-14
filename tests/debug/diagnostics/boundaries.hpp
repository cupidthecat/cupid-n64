#pragma once

#include "core/state/core_state.hpp"
#include "homebrew.hpp"

namespace debug_test {

inline void counter_boundary_probes() {
  auto guest = machine(130);
  DiagnosticConfig config;
  config.profiling = true;
  guest->diagnostics().configure(config);
  auto &cpu = guest->cpu();
  cpu.execute(test::i(9, 0, 7, 42));
  cpu.execute(12);
  cpu.synchronize_timer();
  equal(guest->diagnostics().profile().cpu[0], 2);
  equal(guest->diagnostics().profile().cpu[1], 0);
  guest->diagnostics().clear_profiles();
  cpu.execute(test::i(9, 0, 7, 43));
  cpu.execute(0x42000018);
  cpu.synchronize_timer();
  equal(guest->diagnostics().profile().cpu[0], 2);
  equal(guest->diagnostics().profile().cpu[1], 2);

  guest->diagnostics().set_homebrew(true);
  guest->diagnostics().clear_profiles();
  cpu.state().gpr[1] = ~0ull;
  cpu.state().gpr[2] = 0;
  cpu.execute(test::i(9, 0, 7, 44));
  cpu.execute(emux(0x29));
  equal(cpu.state().gpr[2], 2);

  for (unsigned mode = 0; mode < 3; ++mode) {
    auto machine = debug_test::machine(130);
    auto &processor = machine->cpu();
    const std::array program{emux(0x28, 1, 2, 1), test::i(9, 0, 7, 42), emux(0x28, 1, 2, 2),
                             emux(0x29), 12u};
    code(*machine, program);
    processor.step();
    processor.set_pc(0xffffffff80001000);
    machine->diagnostics().set_homebrew(true);
    if (mode == 0)
      for (unsigned n = 0; n < program.size(); ++n)
        processor.step();
    else if (mode == 1)
      equal(processor.run_interpreted_block(1000), true);
    else
      equal(processor.run_block(1000), true);
    equal(processor.state().gpr[2], 2);

    for (bool global_read : {false, true}) {
      auto sample = debug_test::machine(130);
      auto &core = sample->cpu();
      const std::array stream{test::i(9, 0, 7, 42), global_read ? emux(0x29) : 12u, 12u};
      code(*sample, stream);
      core.step();
      core.set_pc(0xffffffff80001000);
      sample->diagnostics().set_homebrew(true);
      core.state().gpr[1] = ~0ull;
      core.state().gpr[2] = 0;
      if (mode == 0)
        for (unsigned n = 0; n < (global_read ? 3u : 2u); ++n)
          core.step();
      else if (mode == 1)
        equal(core.run_interpreted_block(1000), true);
      else
        equal(core.run_block(1000), true);
      if (global_read)
        equal(core.state().gpr[2], 2);
      equal(sample->diagnostics().profile().cpu[0], global_read ? 3 : 2);
      equal(sample->diagnostics().profile().cpu[1], 0);
    }
  }
}

inline void signal_boundary_probes() {
  for (bool native : {false, true}) {
    auto guest = machine(130);
    auto &rsp = guest->signal();
    DiagnosticConfig config;
    config.profiling = true;
    guest->diagnostics().configure(config);
    const std::array ignored{test::i(35, 0, 1, 0), emux(0x25), 13u};
    for (unsigned n = 0; n < ignored.size(); ++n)
      rsp.write_local(0x1000 + n * 4, 4, ignored[n]);
    rsp.write_io(16, 1);
    rsp.elapse(128);
    if (native)
      rsp.run();
    else
      rsp.run_interpreted();
    const auto profile = guest->diagnostics().profile();
    equal(profile.rsp[0] - profile.rsp[1], 15);

    rsp.power();
    guest->diagnostics().set_homebrew(true);
    const std::array controls{emux(0x2c, 0, 0, 1), emux(0x2c, 0, 0, 3), test::i(9, 0, 7, 42), 13u};
    for (unsigned n = 0; n < controls.size(); ++n)
      rsp.write_local(0x1000 + n * 4, 4, controls[n]);
    rsp.write_io(16, 1);
    rsp.elapse(128);
    const auto run = [&] {
      if (native)
        rsp.run();
      else
        rsp.run_interpreted();
    };
    run();
    equal(guest->diagnostics().control() == HostControl::Exit, true);
    equal(rsp.pc(), 4);
    equal(rsp.state().gpr[7], 0);
    equal(rsp.status().halted, false);
    equal(guest->diagnostics().take_control() == HostControl::Exit, true);
    run();
    equal(guest->diagnostics().control() == HostControl::NormalSpeed, true);
    equal(rsp.pc(), 8);
    equal(rsp.state().gpr[7], 0);
    equal(guest->diagnostics().take_control() == HostControl::NormalSpeed, true);
    run();
    equal(rsp.state().gpr[7], 42);
    equal(rsp.status().halted, true);
  }

  auto guest = machine(141);
  auto &rsp = guest->signal();
  const auto before = CoreState::capture(*guest);
  for (unsigned address = 0; address < 32; address += 4)
    rsp.peek_io(address);
  equal(CoreState::capture(*guest) == before, true);
  equal(rsp.peek_io(28), 0);
  equal(rsp.read_io(28), 0);
  equal(rsp.peek_io(28), 1);
}

} // namespace debug_test
