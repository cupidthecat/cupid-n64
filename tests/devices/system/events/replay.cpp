#include "../reset/fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
#include <bit>

namespace test {
namespace {
struct EventProbe : reset_fixture::Machine {
  void code(unsigned address, unsigned value) {
    console->ram().write(address, 4, value);
  }
  void write_rsp(unsigned address, unsigned value) {
    console->signal().write_local(address, 4, value);
  }
  void prepare_pif() {
    console->pif().ram()[0] = 0xfe;
    for (unsigned n = 1; n < 64; ++n)
      console->pif().ram()[n] = 0;
  }
  void setup_cpu(bool, unsigned mask, unsigned target) {
    console->cpu().set_pc(0xffffffff80001000ull);
    console->cpu().state().gpr[8] = 0;
    console->cpu().state().gpr[9] = 0x100000;
    console->cpu().write_control(cupid::n64::Status, 0x30000000 | (mask >= 2 ? 0x8401 : 0));
    console->cpu().write_control(cupid::n64::Count, 0);
    console->cpu().write_control(cupid::n64::Compare, target);
  }
  void run(bool native, unsigned limit) {
    if (native)
      console->run_interval(limit);
    else
      console->step();
  }
  void run_until(bool native, std::uint64_t deadline) {
    unsigned attempts = 0;
    while (elapsed() < deadline) {
      if (++attempts > 1000000)
        throw std::runtime_error("CPU event execution did not reach deadline");
      run(native, 128);
    }
  }
  void write_compare(unsigned value) {
    console->cpu().write_control(cupid::n64::Compare, value);
  }
  std::uint64_t elapsed() {
    return console->cpu().state().clocks;
  }
  std::int64_t video_clock() {
    return console->video().clocks();
  }
  unsigned video_fraction() {
    return console->video().fraction();
  }
  std::int64_t audio_clock() {
    return console->audio().clocks();
  }
  std::uint64_t audio_left() {
    return std::bit_cast<std::uint64_t>(console->audio().output().left);
  }
  std::uint64_t audio_right() {
    return std::bit_cast<std::uint64_t>(console->audio().output().right);
  }
  std::int64_t rsp_clock() {
    return console->signal().clocks();
  }
  std::int64_t rsp_dma_clock() {
    return console->signal().dma_clocks();
  }

  void finish() {
    reset_fixture::Machine::finish(cpu_events::expected, "CPU device events");
  }
};
} // namespace
void cpu_event_tests() {
  EventProbe probe;
  cpu_events::scenarios(probe);
  equal(probe.block, cpu_events::expected.size());
  equal(probe.observations, 1163280);
}
} // namespace test
