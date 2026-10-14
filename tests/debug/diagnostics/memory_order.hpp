#pragma once

#include "memory_output.hpp"

namespace debug_test {

inline void homebrew_memory_order_probes() {
  for (bool enabled : {false, true})
    for (unsigned mode = 0; mode < 3; ++mode)
      for (unsigned cache = 0; cache < 3; ++cache)
        for (unsigned function : {0x25u, 0x27u}) {
          auto console = machine(130);
          auto &cpu = console->cpu();
          console->diagnostics().set_homebrew(enabled);
          raw_word(*console, 0x2000, 0x41424344);
          raw_word(*console, 0x2004, 0x45464748);
          cpu.write_control(Index, 0);
          cpu.write_control(PageMask, 0);
          cpu.write_control(EntryHi, 0x2000);
          cpu.write_control(EntryLo0, (2 << 6) | 7 | ((cache ? 3 : 2) << 3));
          cpu.write_control(EntryLo1, (3 << 6) | 7 | ((cache ? 3 : 2) << 3));
          cpu.execute(0x42000002);
          if (cache == 2) {
            cpu.state().gpr[4] = 0xffffffff80002000;
            cpu.execute(test::i(35, 4, 5, 0));
          }
          cpu.write_control(Status, 0x34000000 | (mode ? 0x10 : 0) | (mode == 2 ? 1 << 25 : 0));
          const auto address = mode    ? 0x2000ull
                               : cache ? 0xffffffff80002000ull
                                       : 0xffffffffa0002000ull;
          const auto before = cpu.state().clocks;
          const auto output = memory_output(*console, DebugProcessor::Cpu, function, address, 8,
                                            function == 0x25 ? 1 : 0);
          equal(cpu.state().clocks - before, 2);
          equal(cpu.read_control(Cause), 0);
          if (!enabled)
            equal(output.empty(), true);
          else if (function == 0x25)
            equal(output == "ABCDEFGH", true);
          else {
            equal(output.find("41 42 43 44 45 46 47 48") != std::string::npos, true);
            equal(output.find("|ABCDEFGH        |") != std::string::npos, true);
          }
          for (unsigned n = 0; n < 8; ++n)
            equal(Inspection::data_byte(*console, DebugProcessor::Cpu, address + n) ==
                      std::optional<std::uint8_t>(mode == 2 ? 'H' - n : 'A' + n),
                  true);
          cpu.execute(test::i(32, 1, 5, 0));
          equal(cpu.state().gpr[5], mode == 2 ? 'H' : 'A');
        }
}

} // namespace debug_test
