#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_priority_tests() {
  cpu_replay::Probe p(cpu_replay::priority::expected, "CPU priority");
  cpu_replay::priority::scenarios(p);
  p.complete(cpu_replay::priority::cases, cpu_replay::priority::observations);
}
} // namespace test
