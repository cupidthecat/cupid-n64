#pragma once

#include "homebrew.hpp"

namespace debug_test {

inline void bus_exception_probes() {
  for (unsigned mode = 0; mode < 3; ++mode)
    for (unsigned kind = 1; kind <= 3; ++kind)
      for (unsigned setting = 0; setting < 4; ++setting)
        for (bool delay : {false, true})
          for (bool boot_vector : {false, true})
            for (bool nested : {false, true}) {
              auto console = machine(130);
              auto &cpu = console->cpu();
              auto &state = cpu.state();
              raw_word(*console, 0x2000, 0x13579bdf);
              state.gpr[1] = 0xffffffff80002000;
              cpu.execute(test::i(35, 1, 5, 0));
              console->diagnostics().set_homebrew(setting != 0);
              state.gpr[2] = setting == 1 ? 0 : 1ull << (setting == 3 ? kind % 3 + 1 : kind);
              cpu.execute(emux(0x2a));
              const auto status = 0x34000000u | (boot_vector ? 0x00400000u : 0) | (nested ? 2u : 0);
              cpu.write_control(Status, status);
              cpu.write_control(Epc, 0xffffffff8000ab00);
              cpu.set_pc(0xffffffff80001000);
              state.gpr[1] = kind == 1   ? 0xffffffff84002000
                             : kind == 2 ? 0xffffffffa4300000
                                         : 0xffffffffa40c0000;
              state.gpr[5] = 0xabcdef;
              state.gpr[31] = 0xffffffff80004000;
              const auto fault = test::i(kind == 2 ? 55 : 35, 1, 5, 0);
              const std::array program{delay ? test::i(4, 0, 0, 7) : fault,
                                       delay ? fault : test::r(8, 31, 0, 0),
                                       delay ? test::r(8, 31, 0, 0) : 0u, 0u};
              code(*console, program);
              if (mode == 0) {
                cpu.step();
                if (delay)
                  cpu.step();
              } else if (mode == 1)
                equal(cpu.run_interpreted_block(state.clocks), true);
              else
                equal(cpu.run_block(state.clocks), true);
              const bool trapped = setting == 2;
              equal(console->frozen(), !trapped);
              equal((cpu.read_control(Cause) >> 2) & 31, trapped ? 24 : 0);
              equal(cpu.read_control(CacheError), trapped ? kind : 0);
              equal(state.gpr[5], kind == 1 ? 0x13579bdf : 0);
              if (trapped) {
                equal(state.pc, boot_vector ? 0xffffffffbfc00380 : 0xffffffff80000180);
                equal(cpu.read_control(Epc), nested ? 0xffffffff8000ab00 : 0xffffffff80001000);
                equal((cpu.read_control(Cause) >> 31) & 1, delay && !nested);
                equal(cpu.read_control(Status), status | 2);
              }
            }
}

inline void cache_bus_exception_probes() {
  for (unsigned mode = 0; mode < 3; ++mode)
    for (unsigned operation = 0; operation < 5; ++operation) {
      auto console = machine(130);
      auto &cpu = console->cpu();
      auto &state = cpu.state();
      console->diagnostics().set_homebrew(true);
      state.gpr[2] = 2;
      cpu.execute(emux(0x2a));
      state.gpr[1] = 0xffffffff84002000;
      state.gpr[5] = 0x2468ace0;
      raw_word(*console, 0x2000, 0x13579bdf);
      if (operation == 2)
        cpu.execute(test::i(47, 1, 0x14, 0));
      if (operation >= 3) {
        cpu.execute(test::i(43, 1, 5, 0));
        cpu.write_control(TagLo, 0x00400280);
        cpu.execute(test::i(47, 1, 0x09, 0));
      }
      cpu.write_control(Status, 0x34000000);
      cpu.set_pc(0xffffffff80001000);
      const auto instruction = operation == 0   ? test::i(43, 1, 5, 0)
                               : operation == 1 ? test::i(47, 1, 0x14, 0)
                               : operation == 2 ? test::i(47, 1, 0x18, 0)
                               : operation == 3 ? test::i(47, 1, 0x19, 0)
                                                : test::i(35, 1, 5, 0);
      if (operation == 4)
        state.gpr[1] = 0xffffffff80002000;
      state.gpr[31] = 0xffffffff80004000;
      const std::array program{instruction, test::r(8, 31, 0, 0), 0u};
      code(*console, program);
      if (mode == 0)
        cpu.step();
      else if (mode == 1)
        equal(cpu.run_interpreted_block(state.clocks), true);
      else
        equal(cpu.run_block(state.clocks), true);
      equal(console->frozen(), false);
      equal((cpu.read_control(Cause) >> 2) & 31, 24);
      equal(cpu.read_control(CacheError), 1);
      equal(cpu.read_control(Epc), 0xffffffff80001000);
      equal(state.pc, 0xffffffff80000180);
      equal(state.gpr[5], operation == 4 ? 0x13579bdf : 0x2468ace0);
      cpu.write_control(Status, 0x34000000);
      cpu.set_pc(0xffffffff80001000);
      if (operation == 0 || operation == 3) {
        cpu.execute(test::i(35, 1, 5, 0));
        equal(state.gpr[5], 0x2468ace0);
        equal(state.pc, operation == 0 ? 0xffffffff80000180 : 0xffffffff80001004);
      }
      if (operation == 1 || operation == 2) {
        cpu.execute(test::i(47, 1, 0x04, 0));
        equal(cpu.read_control(TagLo), 0x00400280);
      }
    }
}

} // namespace debug_test
