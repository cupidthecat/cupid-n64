#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_cop2_memory_tests() {
  cpu_replay::Probe p(cpu_replay::cop2_memory::expected, "CPU COP2 memory modes");
  cpu_replay::cop2_memory::scenarios(p);
  p.complete(cpu_replay::cop2_memory::cases, cpu_replay::cop2_memory::observations);
}
} // namespace test
