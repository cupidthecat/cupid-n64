#include "../fixture.hpp"
#include "expected.hpp"
#include "fetch.hpp"
#include "fetch_expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct TranslationProbe : cpu_replay::Probe {
  using Probe::Probe;
  std::uint64_t current_pc() {
    return cpu.state().pc;
  }
};
} // namespace

void cpu_replay_translation_tests() {
  using namespace cpu_replay::translation;
  std::vector<std::uint64_t> values(route0.begin(), route0.end());
  values.insert(values.end(), route1.begin(), route1.end());
  unsigned routes = 2;
#if defined(_M_X64) || defined(__x86_64__)
  values.insert(values.end(), route2.begin(), route2.end());
  routes = 3;
#endif
  TranslationProbe data(values, "CPU address translation");
  scenarios(data, routes);
  data.complete(routes * 138240, std::uint64_t(routes) * 369446400);

  values.assign(fetch::route0.begin(), fetch::route0.end());
  routes = 1;
#if defined(_M_X64) || defined(__x86_64__)
  values.insert(values.end(), fetch::route1.begin(), fetch::route1.end());
  routes = 2;
#endif
  TranslationProbe code(values, "CPU instruction translation");
  fetch::scenarios(code, routes);
  code.complete(routes * 24576, std::uint64_t(routes) * 66674688);
}
} // namespace test
