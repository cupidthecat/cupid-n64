#include "../../system/cpu/fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Probe : cpu_bus::Machine {
  void reset(bool compiled) {
    create(cupid::n64::ConsoleConfig{}, compiled);
    load_rom();
  }
  void signal_write(unsigned address, unsigned value) {
    console->signal().write_local(address, 4, value);
  }
  auto signal_word(unsigned address) {
    return console->signal().read_local(address, 8);
  }
  void finish() {
    cpu_bus::Machine::finish(cpu_rdp::expected, "RDP CPU");
  }
};
} // namespace
void rdp_cpu_tests() {
  Probe probe;
  cpu_rdp::scenarios(probe);
  equal(probe.block, cpu_rdp::expected.size());
  equal(probe.observations, 1504704);
}
} // namespace test
