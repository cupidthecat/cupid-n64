#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
void cpu_replay_decode_tests() {
  cpu_replay::Probe p(cpu_replay::decode::expected, "CPU decode");
  cpu_replay::decode::scenarios(p);
  p.complete(cpu_replay::decode::cases, cpu_replay::decode::observations);
}
} // namespace test
