#pragma once

#include "../support/test.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"

namespace test {

inline void rsp_snapshot_schedule_tests() {
#if defined(_M_X64) || defined(_M_ARM64) || defined(__x86_64__) || defined(__aarch64__)
  using namespace cupid::n64;
  for (bool exposed : {false, true})
    for (unsigned action = 0; action < 4; ++action) {
      Console machine({.random_seed = 0});
      auto &rsp = machine.signal();
      if (exposed)
        static_cast<void>(rsp.imem());
      rsp.write_local(0x1000, 4, 0x243f0001);
      rsp.write_local(0x1004, 4, 0x08000000);
      rsp.write_local(0x1008, 4, 0x8c010000);
      rsp.write_local(0, 4, 41);
      rsp.write_io(16, 1);
      rsp.advance(1);
      equal(rsp.clocks(), 11);
      equal(rsp.pc(), 0);
      equal(rsp.state().gpr[31], 1);
      std::vector<std::uint8_t> saved;
      if (action == 1) {
        saved = CoreState::capture(machine);
        equal(CoreState::capture(machine) == saved, true);
      } else if (action >= 2) {
        saved = CoreState::checkpoint(machine).bytes;
        if (action == 3) {
          auto invalid = saved;
          invalid.back() ^= 1;
          bool rejected = false;
          try {
            CoreState::restore(machine, invalid);
          } catch (const state::InvalidState &) {
            rejected = true;
          }
          equal(rejected, true);
        }
      }
      equal(rsp.clocks(), 11);
      equal(rsp.pc(), 0);
      equal(rsp.state().gpr[31], 1);
      rsp.advance(12);
      equal(rsp.clocks(), action == 1 ? 14 : 11);
      equal(rsp.pc(), 0);
      equal(rsp.state().gpr[31], 42);
      if (action == 1 || action == 2) {
        CoreState::restore(machine, saved);
        rsp.advance(12);
        equal(rsp.clocks(), 14);
        equal(rsp.pc(), 0);
        equal(rsp.state().gpr[31], 42);
        Console fresh({.random_seed = 0});
        CoreState::restore(fresh, saved);
        fresh.signal().advance(12);
        equal(fresh.signal().clocks(), 14);
        equal(fresh.signal().pc(), 0);
        equal(fresh.signal().state().gpr[31], 42);
      }
    }
#endif
}

} // namespace test
