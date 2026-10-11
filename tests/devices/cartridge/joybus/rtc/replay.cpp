#include "../../cpu/fixture.hpp"
#include "../scenarios.hpp"
#include "expected.hpp"

namespace test {
namespace {
struct Probe : save_cpu::Machine {
  void finish() {
    save_cpu::Machine::finish(cpu_rtc::expected, "RTC CPU");
  }
};
} // namespace
void rtc_cpu_tests() {
  Probe probe;
  cpu_save_joybus::rtc(probe);
  equal(probe.block, cpu_rtc::expected.size());
  equal(probe.observations, 8445024);
}
} // namespace test
