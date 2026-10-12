#pragma once

#include "../fixture.hpp"
#include "core/debug/homebrew/command.hpp"

namespace debug_test {

inline std::uint32_t emux(unsigned function, unsigned rd = 1, unsigned rt = 2,
                          unsigned option = 0) {
  return 0x42000000 | (rd << 20) | (rt << 15) | (option << 6) | function;
}

inline void homebrew_cpu(unsigned mode, bool enabled, std::uint32_t instruction, std::uint64_t rd,
                         std::uint64_t rt, const std::function<void(Console &)> &verify) {
  auto console = machine(130);
  const std::array program{instruction, 0x0000000cu};
  code(*console, program);
  raw_word(*console, 0x2000, 0x48692100);
  console->cpu().state().gpr[1] = rd;
  console->cpu().state().gpr[2] = rt;
  console->diagnostics().set_homebrew(enabled);
  if (mode == 0) {
    console->cpu().step();
    console->cpu().step();
  } else if (mode == 1)
    equal(console->cpu().run_interpreted_block(1000), true);
  else
    equal(console->cpu().run_block(1000), true);
  verify(*console);
}

inline void homebrew_probes() {
  equal(HomebrewCommand::capabilities(DebugProcessor::Cpu), 0x17a1);
  equal(HomebrewCommand::capabilities(DebugProcessor::Rsp), 0x17f9);
  for (unsigned mode = 0; mode < 3; ++mode) {
    for (unsigned option = 0; option < 4; ++option)
      for (bool enabled : {false, true})
        homebrew_cpu(mode, enabled, emux(0x20, 1, 2, option), 0xabcdef, 0x123456,
                     [&](Console &console) {
                       equal(console.cpu().state().gpr[1], enabled ? option == 1   ? 0x17a1
                                                                     : option == 2 ? 0x0e
                                                                                   : 0
                                                                   : 0xabcdef);
                       equal(console.cpu().state().gpr[2], 0x123456);
                       equal(console.cpu().state().gpr[0], 0);
                     });
    for (unsigned function : {0x25u, 0x27u})
      for (bool enabled : {false, true})
        homebrew_cpu(mode, enabled, emux(function), 0xffffffff80002000, 4, [&](Console &console) {
          const auto records = console.diagnostics().records();
          equal(records.size(), enabled ? 1 : 0);
          if (!records.empty()) {
            equal(records[0].text.find(function == 0x25 ? "Hi!" : "48 69 21 00") !=
                      std::string::npos,
                  true);
            equal(records[0].text.size() <= Diagnostics::maximum_text, true);
            equal(records[0].origin.pc, 0xffffffff80001000);
          }
        });
    homebrew_cpu(mode, true, emux(0x29), ~0ull, 0x311,
                 [&](Console &console) { equal(console.cpu().state().gpr[2], 32); });
    for (unsigned control = 1; control < 4; ++control)
      homebrew_cpu(mode, true, emux(0x2c, 1, 2, control), 0, 0, [&](Console &console) {
        equal(static_cast<unsigned>(console.diagnostics().take_control()), control);
        equal(static_cast<unsigned>(console.diagnostics().take_control()), 0);
      });
  }
  for (bool native : {false, true})
    for (bool enabled : {false, true}) {
      auto console = machine(130);
      auto &rsp = console->signal();
      console->diagnostics().set_homebrew(enabled);
      const std::array program{emux(0x20, 3, 2, 1), emux(0x20, 4, 2, 2), emux(0x23, 1, 2, 2),
                               emux(0x25),          emux(0x26, 1, 2, 3), emux(0x27),
                               emux(0x24),          0x0000000du};
      for (unsigned n = 0; n < program.size(); ++n)
        rsp.write_local(0x1000 + n * 4, 4, program[n]);
      rsp.write_local(0, 4, 0x48692100);
      rsp.state().gpr[2] = 4;
      rsp.state().gpr[3] = 0x1111;
      rsp.state().gpr[4] = 0x2222;
      rsp.write_io(16, 1);
      rsp.elapse(128);
      if (native)
        rsp.run();
      else
        rsp.run_interpreted();
      equal(rsp.status().broken, true);
      equal(rsp.state().gpr[3], enabled ? 0x17f9 : 0x1111);
      equal(rsp.state().gpr[4], enabled ? 0x0e : 0x2222);
      const auto records = console->diagnostics().records();
      equal(records.size(), enabled ? 3 : 0);
      if (!records.empty()) {
        equal(records[0].text == "Hi!", true);
        equal(records[1].text.find("R3 = 000017f9") != std::string::npos, true);
        equal(records[2].text.find("48 69 21 00") != std::string::npos, true);
      }
      const auto trace = console->tracing().records();
      equal(trace.size(), enabled ? 2 : 0);
      if (!trace.empty()) {
        equal(trace[0].pc, 12);
        equal(trace[1].pc, 16);
      }
      equal(console->tracing().enabled(TraceKind::RspInstruction), false);
    }
}

} // namespace debug_test
