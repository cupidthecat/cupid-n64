#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct ReuseProbe : reset_fixture::Machine {
  void finish() {
    reset_fixture::Machine::finish(reset_reuse::expected, "Native reset reuse");
  }
  void install(unsigned phase, unsigned variant) {
    for (unsigned n = 0; n < 6; ++n)
      console->ram().write(test::reset_reuse::CpuCode + n * 4, 4,
                           test::reset_reuse::cpu_code(phase, variant)[n]);
    for (unsigned n = 0; n < 4; ++n)
      console->signal().write_local(0x1000 + n * 4, 4,
                                    test::reset_reuse::rsp_code(phase, variant)[n]);
    console->cpu().write_control(cupid::n64::Status, 0x30000000);
    console->cpu().set_pc(0xffffffff80000000ull + test::reset_reuse::CpuCode);
    console->cpu().state().gpr[1] = 0xffffffffa0000000ull + test::reset_reuse::Data;
    console->cpu().state().gpr[31] = 0xffffffff80007800ull;
  }
  void execute_cpu() {
    for (unsigned call = 0; console->cpu().state().pc != 0xffffffff80007800ull; ++call) {
      if (call == 8)
        throw std::runtime_error("Reset reuse CPU did not finish");
      if (!console->cpu().run_block(console->cpu().state().clocks))
        throw std::runtime_error("Reset reuse CPU native block unavailable");
    }
  }
  void execute_rsp() {
    auto &rsp = console->signal();
    rsp.write_io(16, 1);
    rsp.elapse(1);
    rsp.run();
    if (!rsp.status().halted || !rsp.status().broken)
      throw std::runtime_error("Reset reuse RSP did not break");
  }
};
} // namespace

void reset_reuse_tests() {
  ReuseProbe probe;
  reset_reuse::scenarios(probe);
  equal(probe.block, reset_reuse::expected.size());
  equal(probe.observations, 18624);
}
} // namespace test
