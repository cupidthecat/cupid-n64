#include "../fixture.hpp"
#include <memory>

namespace test {
using namespace cupid::n64;

void rsp_pressure_tests() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (unsigned count : {2250u, 2280u, 2290u, 2293u, 2294u, 2300u, 2350u}) {
    auto fixture = std::make_unique<RspFixture>();
    auto &rsp = fixture->rsp;
    rsp.write_local(0, 4, 41);
    rsp.write_word(0x1000, 0x243f0001);
    rsp.write_word(0x1004, 0x08000000);
    rsp.write_word(0x1008, 0x8c010000);
    rsp.write_io(16, 1);
    rsp.advance(1);
    equal(rsp.clocks() + 1, 12);
    for (unsigned n = 0; n < count; ++n) {
      rsp.write_word(0x1100, 0x24070000 | n);
      rsp.write_word(0x1104, 13);
      rsp.write_status(0, 0x100);
      rsp.write_io(16, 5);
      rsp.advance(static_cast<unsigned>(rsp.clocks() + 1));
    }
    rsp.write_word(0x1200, 0x8c010000);
    rsp.write_word(0x1204, 0);
    rsp.write_status(0, 0x200);
    rsp.write_io(16, 5);
    rsp.step();
    rsp.step();
    rsp.write_status(0, 0);
    rsp.advance(static_cast<unsigned>(rsp.clocks() + 1));
    equal(rsp.clocks() + 1, count < 2294 ? 12 : 15);
    equal(rsp.state().gpr[1], 41);
    equal(rsp.state().gpr[31], 42);
    equal(rsp.pc(), 0);
    equal(rsp.status().halted, false);
  }
#endif
}

} // namespace test
