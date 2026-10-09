#include "../../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_endian_linked_tests() {
  cpu_replay::Probe p(cpu_replay::endian::linked::expected, "CPU endian LL/SC");
  cpu_replay::endian::linked::scenarios(p);
  p.complete(cpu_replay::endian::linked::cases, cpu_replay::endian::linked::observations);
}
} // namespace test
