#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_pipeline_tests() {
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
}

} // namespace test
