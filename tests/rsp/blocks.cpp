#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_block_tests() {
  for (bool taken : {false, true}) {
    RspFixture f;
    f.rsp.write_local(0x1000, 4, i(9, 0, 1, 7));
    f.rsp.write_local(0x1004, 4, i(4, taken ? 0 : 1, 0, 2));
    f.rsp.write_local(0x1008, 4, i(9, 0, 2, 9));
    f.rsp.write_local(0x100c, 4, i(9, 0, 3, 11));
    f.rsp.write_local(0x1010, 4, 13);
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(f.rsp.pc(), taken ? 16 : 12);
    equal(f.rsp.clocks(), taken ? 11 : 8);
    equal(f.rsp.state().gpr[1], 7);
    equal(f.rsp.state().gpr[2], 9);
    equal(f.rsp.state().gpr[3], 0);
  }
  {
    RspFixture f;
    f.rsp.write_local(0x1ffc, 4, 0x08000002);
    f.rsp.write_local(0x1000, 4, i(9, 0, 2, 17));
    f.rsp.write_status(0, 0xffc);
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(f.rsp.pc(), 8);
    equal(f.rsp.clocks(), 8);
    equal(f.rsp.state().gpr[2], 17);
  }
  {
    RspFixture f;
    f.rsp.state().gpr[31] = 32;
    f.rsp.write_local(0x1000, 4, r(9, 31, 0, 31, 0));
    f.rsp.write_local(0x1004, 4, i(9, 0, 2, 19));
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(f.rsp.pc(), 32);
    equal(f.rsp.clocks(), 8);
    equal(f.rsp.state().gpr[31], 8);
    equal(f.rsp.state().gpr[2], 19);
  }
  {
    RspFixture f;
    f.rsp.write_local(0x1000, 4, i(9, 1, 2, 1));
    f.rsp.write_local(0x1004, 4, 0x08000000);
    f.rsp.write_local(0x1008, 4, i(35, 0, 1, 0));
    f.rsp.write_local(0, 4, 41);
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(f.rsp.clocks(), 11);
    equal(f.rsp.state().gpr[2], 1);
    f.rsp.advance(12);
    equal(f.rsp.clocks(), 11);
    equal(f.rsp.state().gpr[2], 42);
    f.rsp.write_local(0x1000, 4, i(9, 1, 2, 1));
    f.rsp.advance(12);
    equal(f.rsp.clocks(), 14);
    equal(f.rsp.state().gpr[2], 42);
    f.rsp.write_local(0x1000, 4, i(9, 1, 2, 2));
    f.rsp.advance(15);
    equal(f.rsp.clocks(), 14);
    equal(f.rsp.state().gpr[2], 43);
    auto memory = f.rsp.imem();
    memory[3] = 3;
    f.rsp.advance(15);
    equal(f.rsp.clocks(), 14);
    equal(f.rsp.state().gpr[2], 44);
    memory[3] = 1;
    f.rsp.advance(15);
    equal(f.rsp.clocks(), 14);
    equal(f.rsp.state().gpr[2], 42);
  }
  {
    RspFixture f;
    for (unsigned address = 0; address < 4096; address += 4)
      f.rsp.write_local(0x1000 | address, 4, i(9, 1, 1, 1));
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(f.rsp.pc(), 0);
    equal(f.rsp.clocks(), 3071);
    equal(f.rsp.state().gpr[1], 1024);
  }
  {
    RspFixture f;
    f.rsp.state().gpr[1] = 2;
    f.rsp.write_local(0x1000, 4, c(4, 1, 4));
    f.rsp.write_local(0x1004, 4, i(9, 0, 2, 23));
    f.rsp.write_local(0x1008, 4, 13);
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(f.rsp.pc(), 0);
    equal(f.rsp.clocks(), 2);
    equal(f.rsp.status().halted, true);
    equal(f.rsp.state().gpr[2], 0);
    f.rsp.write_io(16, 1);
    f.rsp.advance(3);
    equal(f.rsp.pc(), 0);
    equal(f.rsp.clocks(), 2);
    equal(f.rsp.status().halted, true);
    equal(f.rsp.state().gpr[2], 0);
    f.rsp.state().gpr[1] = 0;
    f.rsp.write_io(16, 1);
    f.rsp.advance(3);
    equal(f.rsp.pc(), 12);
    equal(f.rsp.status().broken, true);
    equal(f.rsp.state().gpr[2], 23);
  }
  {
    RspFixture f;
    unsigned calls = 0;
    f.rsp.connect_display({}, [&](unsigned reg, std::uint32_t value) {
      ++calls;
      equal(reg, 0);
      equal(value, 37);
      equal(f.rsp.clocks(), 2);
    });
    f.rsp.write_local(0x1000, 4, i(9, 0, 1, 37));
    f.rsp.write_local(0x1004, 4, c(4, 1, 8));
    f.rsp.write_local(0x1008, 4, 13);
    f.rsp.write_io(16, 1);
    f.rsp.advance(1);
    equal(calls, 1);
    equal(f.rsp.status().broken, true);
  }
}

} // namespace test
