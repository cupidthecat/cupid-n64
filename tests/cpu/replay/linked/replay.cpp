#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_linked_tests() {
  cpu_replay::Probe p(cpu_replay::linked::expected, "CPU linked");
  cpu_replay::linked::scenarios(p);
  p.complete(cpu_replay::linked::cases, cpu_replay::linked::observations);
}
} // namespace test
