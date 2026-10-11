#include "../../cpu/fixture.hpp"
#include "../scenarios.hpp"
#include "expected.hpp"

namespace test {
namespace {
struct Probe : save_cpu::Machine {
  void finish() {
    save_cpu::Machine::finish(cpu_eeprom::expected, "EEPROM CPU");
  }
};
} // namespace
void eeprom_cpu_tests() {
  Probe probe;
  cpu_save_joybus::eeprom(probe);
  equal(probe.block, cpu_eeprom::expected.size());
  equal(probe.observations, 8443872);
}
} // namespace test
