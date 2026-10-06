#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_memory_tests() {
  RspFixture f;
  auto &rsp = f.rsp;
  for (unsigned n = 0; n < 4096; ++n)
    rsp.dmem()[n] = static_cast<std::uint8_t>(n);
  rsp.state().gpr[1] = 0xffe;
  rsp.state().vectors[2].lanes.fill(0xffff);
  rsp.execute(vector_memory(false, 3, 2, 1, 13, 0));
  equal(rsp.state().vectors[2].byte(12), 255);
  equal(rsp.state().vectors[2].byte(13), 254);
  equal(rsp.state().vectors[2].byte(14), 255);
  equal(rsp.state().vectors[2].byte(15), 0);
  rsp.execute(vector_memory(true, 3, 2, 1, 13, 0));
  equal(rsp.dmem()[0xffe], 254);
  equal(rsp.dmem()[0xfff], 255);
  equal(rsp.dmem()[0], 0);
  equal(rsp.dmem()[1], 255);
  rsp.state().gpr[1] = 0x105;
  rsp.execute(vector_memory(false, 4, 2, 1, 0, 0));
  equal(rsp.state().vectors[2].byte(0), 5);
  equal(rsp.state().vectors[2].byte(10), 15);
  equal(rsp.state().vectors[2].byte(11), 255);
  rsp.execute(vector_memory(false, 5, 2, 1, 0, 0));
  equal(rsp.state().vectors[2].byte(11), 0);
  equal(rsp.state().vectors[2].byte(15), 4);
  rsp.execute(vector_memory(false, 6, 2, 1, 0, 0));
  equal(rsp.state().vectors[2].lanes[0], 0x0500);
  equal(rsp.state().vectors[2].lanes[7], 0x0c00);
  rsp.execute(vector_memory(false, 7, 2, 1, 0, 0));
  equal(rsp.state().vectors[2].lanes[0], 0x0280);
  rsp.state().gpr[1] = 0x108;
  rsp.execute(vector_memory(false, 11, 13, 1, 2, 0));
  equal(rsp.state().vectors[9].lanes[0], 0x1213);
  equal(rsp.state().vectors[10].lanes[1], 0x1415);
  equal(rsp.state().vectors[8].lanes[7], 0x1011);
  rsp.state().gpr[1] = 0x200;
  rsp.execute(vector_memory(true, 11, 13, 1, 2, 0));
  equal(rsp.dmem()[0x20e], 16);
  equal(rsp.dmem()[0x20f], 17);
  equal(rsp.dmem()[0x200], 18);
  equal(rsp.dmem()[0x201], 19);
  rsp.state().gpr[1] = 0x300;
  rsp.state().vectors[2].lanes = {0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800};
  rsp.execute(vector_memory(true, 9, 2, 1, 1, 0));
  equal(rsp.dmem()[0x300], 14);
  equal(rsp.dmem()[0x304], 16);
  equal(rsp.dmem()[0x308], 10);
  equal(rsp.dmem()[0x30c], 12);
  rsp.execute(vector_memory(true, 9, 2, 1, 2, 0));
  equal(rsp.dmem()[0x300], 0);
  equal(rsp.dmem()[0x304], 0);
}

} // namespace test
