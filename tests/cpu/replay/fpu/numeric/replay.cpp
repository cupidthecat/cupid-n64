#include "../../fixture.hpp"
#include "environment.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct NumericProbe : cpu_replay::Probe {
  using Probe::Probe;
  unsigned executions = 0;
  void fpu_control(unsigned value) {
    cpu.state().fcr31 = value | 0x0003f000;
  }
  void execute(unsigned instruction) {
    const cpu_replay::fpu::numeric::HostEnvironment environment(executions++);
    Probe::execute(instruction);
  }
  void run(bool native, std::uint64_t start, std::uint64_t end) {
    const cpu_replay::fpu::numeric::HostEnvironment environment(executions++);
    Probe::run(native, start, end);
  }
};
} // namespace

void cpu_replay_fpu_numeric_tests() {
  using namespace cpu_replay::fpu::numeric;
  std::vector<std::uint64_t> expected(direct.begin(), direct.end());
  expected.insert(expected.end(), scalar.begin(), scalar.end());
  unsigned routes = 2;
#if defined(_M_X64) || defined(__x86_64__)
  expected.insert(expected.end(), native.begin(), native.end());
  routes = 3;
#endif
  NumericProbe probe(expected, "FPU numeric replay");
  scenarios(probe, routes);
  probe.complete(routes * 82560, std::uint64_t(routes) * 17585280);
}
} // namespace test
