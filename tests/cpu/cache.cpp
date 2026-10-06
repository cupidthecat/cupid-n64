#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;

void cache_tests() {
  {
    Fixture f;
    f.cpu.set_pc(0xffffffff80001000);
    f.code(0, i(9, 0, 1, 1));
    f.code(4, i(9, 0, 2, 2));
    f.cpu.step();
    equal(f.cpu.state().gpr[1], 1);
    equal(f.memory.transfers.size(), 8);
    equal(f.cpu.state().clocks, 98);
    f.memory.put(0x1004, 4, i(9, 0, 2, 9));
    f.cpu.step();
    equal(f.cpu.state().gpr[2], 2);
    equal(f.memory.transfers.size(), 8);
    f.cpu.set_pc(0xffffffff80001004);
    f.cpu.state().gpr[3] = 0xffffffff80001000;
    f.cpu.execute(i(0x2f, 3, 0x10, 0));
    f.cpu.set_pc(0xffffffff80001004);
    f.cpu.step();
    equal(f.cpu.state().gpr[2], 9);
    equal(f.memory.transfers.size(), 16);
  }
  {
    Fixture f;
    f.memory.put(0x2000, 8, 0x0123456789abcdef);
    f.cpu.state().gpr[1] = 0xffffffff80002000;
    f.cpu.execute(i(0x37, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0x0123456789abcdef);
    equal(f.memory.transfers.size(), 4);
    equal(f.cpu.state().clocks, 82);
    f.cpu.execute(i(0x24, 1, 2, 7));
    equal(f.cpu.state().gpr[2], 0xef);
    equal(f.cpu.state().clocks, 86);
    f.cpu.state().gpr[2] = 0xff;
    f.cpu.execute(i(0x28, 1, 2, 2));
    equal(f.memory.get(0x2000, 8), 0x0123456789abcdef);
    f.cpu.execute(i(0x37, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0x0123ff6789abcdef);
    f.cpu.execute(i(0x2f, 1, 0x19, 0));
    equal(f.memory.get(0x2000, 8), 0x0123ff6789abcdef);
    equal(f.memory.transfers.size(), 8);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffff80002000;
    f.cpu.state().gpr[2] = 0xdeadbeef;
    f.cpu.execute(i(0x2b, 1, 2, 0));
    f.cpu.state().gpr[1] = 0xffffffff80004000;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.memory.get(0x2000, 4), 0xdeadbeef);
    equal(f.memory.transfers.size(), 12);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffff80002000;
    f.cpu.write_control(TagLo, 0x00000280);
    f.cpu.execute(i(0x2f, 1, 9, 0));
    f.cpu.execute(i(0x2f, 1, 5, 0));
    equal(f.cpu.read_control(TagLo), 0x000002c0);
    equal(f.memory.transfers.size(), 0);
    f.cpu.execute(i(0x2f, 1, 1, 0));
    f.cpu.execute(i(0x2f, 1, 5, 0));
    equal(f.cpu.read_control(TagLo), 0x00000200);
  }
}

} // namespace test
