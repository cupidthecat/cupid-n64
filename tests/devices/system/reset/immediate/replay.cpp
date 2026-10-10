#include "../fixture.hpp"
#include "expected.hpp"

namespace test {
namespace {
struct ResetProbe : reset_fixture::Machine {
  void finish() {
    reset_fixture::Machine::finish(machine_reset::expected, "Machine reset");
  }
};
} // namespace

void machine_reset_tests() {
  ResetProbe probe;
  machine_reset::scenarios(probe);
  equal(probe.block, machine_reset::expected.size());
  equal(probe.observations, 1096896);
}
} // namespace test
