#include "board_expected.hpp"
#include "board_scenarios.hpp"
#include "fixture.hpp"
namespace test {
namespace {
struct Probe : transfer_fixture::TransferProbe {
  void finish() {
    reset_fixture::Machine::finish(transfer_board::expected, "Transfer Pak board commands");
  }
};
} // namespace
void transfer_board_tests() {
  Probe probe;
  transfer_board::scenarios(probe);
  equal(probe.block, transfer_board::expected.size());
  equal(probe.observations, 13714704);
}
} // namespace test
