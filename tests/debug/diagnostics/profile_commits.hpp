#pragma once

#include "homebrew.hpp"

namespace debug_test {

inline void profile_commit_probes() {
  for (bool enabled : {false, true})
    for (bool exception : {false, true})
      for (unsigned preceding : {0u, 1u, 3u})
        for (unsigned commit = 0; commit < 3; ++commit)
          for (unsigned metric = 0; metric < 2; ++metric) {
            auto console =
                std::make_unique<Console>(ConsoleConfig{.homebrew = enabled, .random_seed = 0});
            auto &cpu = console->cpu();
            cpu.write_control(Status, 0x34000000 | (exception ? 2 : 0));
            cpu.set_pc(0xffffffff80001000);
            for (unsigned n = 0; n < preceding; ++n)
              cpu.execute(0);
            if (commit == 1)
              cpu.execute(0x40084800);
            if (commit == 2) {
              cpu.state().gpr[8] = 0x1000;
              cpu.execute(0x40885800);
            }
            cpu.state().gpr[1] = ~0ull;
            cpu.state().gpr[2] = metric;
            cpu.execute(emux(0x29));
            const auto expected = !enabled                                 ? metric
                                  : !commit || (metric == 1 && !exception) ? 0u
                                                                           : preceding + 1;
            const auto instructions = preceding + unsigned(commit != 0) + 1;
            equal(cpu.state().gpr[2], expected);
            equal(cpu.state().clocks, instructions * 2);
            equal(cpu.state().pc, 0xffffffff80001000 + instructions * 4);
            equal(cpu.read_control(Cause), 0);
          }
}

} // namespace debug_test
