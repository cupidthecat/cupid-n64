#include "../fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_context_tests() {
  for (unsigned reg = 1; reg < 31; ++reg) {
    RspFixture f;
    const auto instruction = i(9, reg, 31, 1);
    f.rsp.write_local(0x1000, 4, instruction);
    f.rsp.write_local(0x1004, 4, 0x08000000);
    f.rsp.write_local(0x1008, 4, i(35, 0, reg, 0));
    f.rsp.write_local(0, 4, 41);
    f.rsp.write_io(16, 1);
    for (unsigned stage = 0; stage < 5; ++stage) {
      if (stage == 2)
        f.rsp.write_local(0x1000, 4, instruction);
      if (stage == 3)
        f.rsp.write_local(0x1000, 4, instruction + 1);
      f.rsp.advance(static_cast<std::uint32_t>(f.rsp.clocks() + 1));
      equal(f.rsp.clocks(), stage < 2 ? 11 : 14);
      equal(f.rsp.pc(), 0);
      equal(f.rsp.state().gpr[31], stage == 0 ? 1 : stage < 3 ? 42 : 43);
    }
  }
}

} // namespace test
