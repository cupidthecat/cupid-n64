#include "../../cpu/fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Probe : save_cpu::Machine {
  void finish() {
    save_cpu::Machine::finish(cpu_sram::expected, "SRAM CPU");
  }
};
} // namespace
void sram_cpu_tests() {
  Probe probe;
  cpu_sram::scenarios(probe);
  equal(probe.block, cpu_sram::expected.size());
  equal(probe.observations, 24481728);
}
} // namespace test
