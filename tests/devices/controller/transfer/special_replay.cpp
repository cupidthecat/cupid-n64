#include "fixture.hpp"
#include "special_expected.hpp"
#include "special_scenarios.hpp"
namespace test {
namespace {
struct Probe : transfer_fixture::TransferProbe {
  void finish() {
    reset_fixture::Machine::finish(transfer_special::expected, "Transfer Pak special commands");
  }
};
} // namespace
void transfer_special_tests() {
  Probe probe;
  transfer_special::scenarios(probe);
  equal(probe.block, transfer_special::expected.size());
  equal(probe.observations, 382920);
}
} // namespace test
