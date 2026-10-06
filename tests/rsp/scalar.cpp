#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_scalar_tests() {
  RspFixture f;
  auto &rsp = f.rsp;
  auto &gpr = rsp.state().gpr;
  gpr[1] = 0x7fffffff;
  gpr[2] = 1;
  equal(rsp.execute(r(32, 1, 2, 3)), 3);
  equal(gpr[3], 0x80000000);
  rsp.execute(i(8, 3, 3, 0xffff));
  equal(gpr[3], 0x7fffffff);
  rsp.execute(r(34, 2, 1, 4));
  equal(gpr[4], 0x80000002);
  rsp.execute(r(3, 0, 4, 5, 31));
  equal(gpr[5], 0xffffffff);
  rsp.execute(r(2, 0, 4, 5, 31));
  equal(gpr[5], 1);
  gpr[1] = 32;
  rsp.execute(r(4, 1, 4, 5));
  equal(gpr[5], 0x80000002);
  rsp.execute(i(11, 5, 6, 0xffff));
  equal(gpr[6], 1);
  rsp.execute(r(1, 1, 4, 5));
  equal(gpr[5], 32);
  rsp.execute(i(15, 0, 0, 0xabcd));
  equal(gpr[0], 0);
  gpr[1] = 0xfff;
  gpr[2] = 0x80fe1234;
  rsp.execute(i(43, 1, 2, 0));
  equal(rsp.dmem()[0xfff], 0x80);
  equal(rsp.dmem()[0], 0xfe);
  equal(rsp.dmem()[1], 0x12);
  equal(rsp.dmem()[2], 0x34);
  rsp.execute(i(33, 1, 3, 0));
  equal(gpr[3], 0xffff80fe);
  rsp.execute(i(35, 1, 3, 0));
  equal(gpr[3], 0x80fe1234);
  rsp.execute(i(37, 1, 3, 0));
  equal(gpr[3], 0x80fe);
  rsp.execute(i(32, 1, 3, 1));
  equal(gpr[3], 0xfffffffe);

  rsp.write_status(0, 0xffc);
  gpr[1] = 1;
  rsp.execute(i(1, 1, 16, 3));
  equal(gpr[31], 4);
  equal(rsp.pc(), 0);
  rsp.execute(i(9, 0, 2, 7));
  equal(rsp.pc(), 4);
  equal(gpr[2], 7);
  rsp.write_status(0, 0x100);
  gpr[1] = 0x321;
  rsp.execute(r(9, 1, 0, 1));
  equal(gpr[1], 0x108);
  equal(rsp.pc(), 0x104);
  equal(rsp.execute(0), 6);
  equal(rsp.pc(), 0x321);
  rsp.write_status(0, 0x200);
  rsp.execute(i(4, 0, 0, 3));
  rsp.execute(i(4, 0, 0, 5));
  equal(rsp.pc(), 0x210);
  rsp.execute(0);
  equal(rsp.pc(), 0x21c);

  rsp.write_io(16, 256);
  rsp.execute(13);
  equal(rsp.status().halted, true);
  equal(rsp.status().broken, true);
  equal(f.mi.read_word(8) & 1, 1);
  rsp.write_io(16, 5 | 8);
  equal(rsp.status().halted, false);
  equal(rsp.status().broken, false);
  equal(f.mi.read_word(8) & 1, 0);
  unsigned calls = 0;
  rsp.connect_display(
      [&](unsigned address) {
        ++calls;
        return 0x12340000 | address;
      },
      [&](unsigned address, std::uint32_t value) {
        ++calls;
        equal(address, 12);
        equal(value, gpr[2]);
      });
  rsp.execute(c(0, 3, 9));
  equal(gpr[3], 0x12340004);
  rsp.execute(c(4, 2, 11));
  equal(calls, 2);
  rsp.execute(c(0, 0, 9));
  equal(gpr[0], 0);
  equal(calls, 3);
}

} // namespace test
