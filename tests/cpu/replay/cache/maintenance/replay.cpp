#include "../../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_cache_maintenance_tests() {
  cpu_replay::Probe probe(cpu_replay::cache::maintenance::expected, "cache maintenance");
  cpu_replay::cache::maintenance::scenarios(probe);
  probe.complete(442368, 248610816);
}
} // namespace test
