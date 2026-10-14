#include "../../../fpu/host/environment.hpp"
#include "../../fixture.hpp"
#include "core/cpu/fpu/rounding.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
#if defined(_M_X64) || defined(__x86_64__)
#if defined(__GNUC__) && !defined(__clang__) && defined(__OPTIMIZE__)
#include "gcc.hpp"
#else
#include "generic.hpp"
#endif
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
#include "native/arm64.hpp"
#if defined(__clang__)
#include "native/rounding.hpp"
#endif
#endif

namespace test {
namespace {
struct NumericProbe : cpu_replay::Probe {
  using Probe::Probe;
  unsigned executions = 0;
  void fpu_control(unsigned value) {
    cpu.state().fcr31 = value | 0x0003f000;
  }
  void execute(unsigned instruction) {
    const cpu_fpu::HostEnvironment environment(executions++);
    Probe::execute(instruction);
  }
  void run(bool native, std::uint64_t start, std::uint64_t end) {
    const cpu_fpu::HostEnvironment environment(executions++);
    Probe::run(native, start, end);
  }
};
} // namespace

void cpu_replay_fpu_numeric_tests() {
  using namespace cpu_replay::fpu::numeric;
  std::vector<std::uint64_t> expected(direct.begin(), direct.end());
  expected.insert(expected.end(), scalar.begin(), scalar.end());
  unsigned routes = 2;
#if defined(_M_ARM64) || defined(__aarch64__)
  expected.insert(expected.end(), arm64_native.begin(), arm64_native.end());
#if defined(__clang__)
  for (const auto &update : arm64_clang_rounding)
    expected[update.group] = update.hash;
#endif
  routes = 3;
#elif defined(_M_X64) || defined(__x86_64__)
  expected.insert(expected.end(), native.begin(), native.end());
  if constexpr (!cupid::n64::floating::vector_integer_rounding)
#if defined(__GNUC__) && !defined(__clang__) && defined(__OPTIMIZE__)
    for (const auto &update : generic_gcc_rounding)
#else
    for (const auto &update : generic_rounding)
#endif
      expected[update.group] = update.hash;
  routes = 3;
#endif
  NumericProbe probe(expected, "FPU numeric replay");
  scenarios(probe, routes);
  probe.complete(routes * 82560, std::uint64_t(routes) * 17585280);
}
} // namespace test
