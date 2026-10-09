#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_memory_tests() {
  cpu_replay::Probe p(cpu_replay::memory::expected, "CPU memory");
  cpu_replay::memory::scenarios(p);
  p.complete(cpu_replay::memory::cases, cpu_replay::memory::observations);
}
} // namespace test
