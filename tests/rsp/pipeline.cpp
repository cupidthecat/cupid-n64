#include "core/system/console.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_pipeline_tests() {
  for (bool homebrew : {false, true}) {
    for (bool native : {false, true}) {
      for (auto instruction : {0x42100025u, 0x42200025u, 0x42100022u}) {
        Console console({.homebrew = homebrew, .random_seed = 0});
        auto &signal = console.signal();
        signal.write_local(0, 4, 41);
        signal.write_local(0x1000, 4, i(35, 0, 1, 0));
        signal.write_local(0x1004, 4, instruction);
        signal.write_local(0x1008, 4, 13);
        signal.write_io(16, 1);
        unsigned clocks = 0;
        if (native) {
          signal.elapse(1);
          signal.run();
          clocks = static_cast<unsigned>(signal.clocks() + 1);
        } else {
          for (unsigned step = 0; step < 3; ++step)
            clocks += signal.step();
        }
        equal(clocks, instruction == 0x42100025 ? 15 : 9);
        equal(signal.pc(), 12);
        equal(signal.state().gpr[1], 41);
        equal(signal.state().gpr[2], 0);
        equal(signal.status().halted, true);
        equal(signal.status().broken, true);
      }
    }
  }
  RspFixture f;
  auto &rsp = f.rsp;
  equal(rsp.execute(i(35, 0, 1, 0)), 3);
  equal(rsp.execute(r(33, 1, 0, 2)), 9);
  equal(rsp.execute(i(35, 0, 3, 0)), 3);
  equal(rsp.execute(i(43, 0, 0, 0)), 3);
  equal(rsp.execute(i(43, 0, 0, 0)), 6);
  rsp.power();
  equal(rsp.execute(vector(0, 1, 2, 3)), 3);
  equal(rsp.execute(vector(0x10, 4, 1, 3)), 12);
  rsp.power();
  rsp.write_local(0x1000, 4, vector(0x2a, 1, 2, 3));
  rsp.write_local(0x1004, 4, i(9, 0, 4, 123));
  equal(rsp.step(), 3);
  equal(rsp.pc(), 8);
  equal(rsp.state().gpr[4], 123);
  rsp.power();
  const auto transfer = (18u << 26) | (4u << 21) | (1u << 16) | (1u << 11);
  rsp.write_local(0x1000, 4, transfer);
  rsp.write_local(0x1004, 4, vector(0x37, 1, 0, 0));
  equal(rsp.step(), 3);
  equal(rsp.pc(), 4);
  rsp.power();
  rsp.write_local(0x1000, 4, transfer);
  rsp.write_local(0x1004, 4, vector(0x37, 2, 0, 0));
  equal(rsp.step(), 3);
  equal(rsp.pc(), 8);
  rsp.power();
  rsp.write_local(0x1000, 4, vector(0x2a, 1, 2, 3));
  rsp.write_local(0x1004, 4, i(4, 0, 0, 4));
  equal(rsp.step(), 3);
  equal(rsp.pc(), 8);
  equal(rsp.step(), 6);
  equal(rsp.pc(), 24);
  rsp.power();
  rsp.advance(129);
  equal(rsp.clocks(), 127);
  rsp.write_io(16, 1);
  rsp.write_local(0x1000, 4, 13);
  rsp.advance(128);
  equal(rsp.status().broken, true);
  equal(rsp.clocks(), 2);
  equal(rsp.pc(), 4);

  for (bool native : {false, true}) {
    for (unsigned cut = 0; cut < 4; ++cut) {
      rsp.power();
      for (unsigned address = 0; address < 4096; address += 4)
        rsp.write_local(0x1000 | address, 4, 0);
      rsp.write_local(0x1000, 4, cut == 1 ? 0 : 0x08000004);
      rsp.write_local(0x1004, 4, cut == 2 ? 0 : 0x08000006);
      rsp.write_local(0x1014, 4, cut == 3 ? 0 : i(9, 2, 2, 1));
      rsp.write_io(16, 1);
      rsp.elapse(64);
      if (native)
        rsp.run();
      else
        rsp.run_interpreted();
      equal(rsp.state().gpr[2], cut == 2 || (cut == 0 && native) ? 1u : 0u);
    }
  }
}

} // namespace test
