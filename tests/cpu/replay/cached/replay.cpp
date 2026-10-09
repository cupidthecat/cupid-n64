#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_cached_tests() {
  cpu_replay::Probe p(cpu_replay::cached::expected, "CPU cached");
  cpu_replay::cached::scenarios(p);
  p.complete(cpu_replay::cached::cases, cpu_replay::cached::observations);
}
} // namespace test
