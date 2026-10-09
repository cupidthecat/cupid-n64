#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_endian_tests() {
  cpu_replay::Probe p(cpu_replay::endian::expected, "CPU endian memory");
  cpu_replay::endian::scenarios(p);
  p.complete(cpu_replay::endian::cases, cpu_replay::endian::observations);
}
} // namespace test
