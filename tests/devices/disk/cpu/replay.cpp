#include "../../fixture.hpp"
#include "core/system/console.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
#include <algorithm>
#include <memory>

namespace test {
namespace {
struct Probe {
  std::unique_ptr<cupid::n64::Console> console;
  unsigned mode = 0, checkpoints = 0;
  unsigned modes() const {
    return 3;
  }
  void reset(unsigned execution) {
    mode = execution;
    cupid::n64::ConsoleConfig config;
    config.disk_drive = true;
    config.random_seed = 0;
    config.disk_clock = [] { return std::int64_t(946684800); };
    console = std::make_unique<cupid::n64::Console>(config);
    std::array<std::uint8_t, 4096> ipl{};
    ipl[0] = 0x80;
    ipl[1] = 0x27;
    ipl[2] = 7;
    ipl[3] = 0x40;
    std::copy_n("NDDJ", 4, ipl.begin() + 0x3b);
    std::array<std::uint8_t, 0x7c0> firmware{};
    if (!console->load_disk(ipl, firmware))
      throw std::runtime_error("Disk trace load failed");
  }
  void initial_write(unsigned address, unsigned value) {
    console->write(address, 4, value);
  }
  void control_write(unsigned n, std::uint64_t value) {
    console->cpu().write_control(n, value);
  }
  void power(bool warm) {
    console->power(warm);
  }
  std::uint64_t access(bool write, unsigned address, unsigned value) {
    auto &cpu = console->cpu();
    cpu.set_pc(0xffffffff80007000ull + (write ? 0 : 0x40));
    cpu.state().gpr[1] = 0xffffffffa0000000ull | address;
    cpu.state().gpr[2] =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(value)));
    cpu.state().gpr[31] = 0xffffffff80007800ull;
    for (unsigned calls = 0; cpu.state().pc != cpu.state().gpr[31]; ++calls) {
      if (calls == 16)
        throw std::runtime_error("Disk trace stream did not finish");
      if (!mode)
        console->step();
      else {
        const auto target = cpu.state().clocks + 1;
        if (!(mode == 1 ? cpu.run_interpreted_block(target) : cpu.run_block(target)))
          throw std::runtime_error("Disk trace compiled block unavailable");
        console->synchronize();
      }
    }
    return cpu.state().gpr[2];
  }
  void idle(unsigned clocks) {
    console->cpu().advance_clocks(clocks);
    console->synchronize();
  }
  auto clock() {
    return console->cpu().state().clocks;
  }
  auto pc() {
    return console->cpu().state().pc;
  }
  auto gpr(unsigned n) {
    return console->cpu().state().gpr[n];
  }
  auto control(unsigned n) {
    return console->cpu().read_control(n);
  }
  auto pi_register(unsigned offset) {
    return console->read(0x04600000 + offset, 4).value;
  }
  auto disk_register(unsigned offset) {
    return console->disk_drive().read_register(offset);
  }

  void observe(unsigned pause, unsigned reset_kind, unsigned phase, std::uint64_t result = 0) {
    std::array<std::uint64_t, disk_cpu::constants.size()> actual{};
    unsigned field = 0;
    actual[field++] = clock();
    actual[field++] = pc();
    actual[field++] = result;
    for (unsigned n = 0; n < 32; ++n)
      actual[field++] = gpr(n);
    for (unsigned n : {8u, 9u, 11u, 12u, 13u, 14u, 16u, 30u})
      actual[field++] = control(n);
    for (unsigned n = 0; n < 15; ++n)
      actual[field++] = pi_register(n * 4);
    for (unsigned n : {0u, 8u, 12u, 16u, 20u, 28u, 40u, 48u, 64u})
      actual[field++] = disk_register(n);
    equal(field, actual.size());
    constexpr std::array<unsigned, 7> pauses{0, 100, 399, 400, 401, 7800, 8000};
    const auto found = std::find(pauses.begin(), pauses.end(), pause);
    equal(found != pauses.end(), true);
    if (found == pauses.end())
      return;
    const auto row =
        ((mode == 2 ? 21u : 0u) + unsigned(found - pauses.begin()) * 3 + reset_kind) * 5 + phase;
    equal(row < disk_cpu::expected.size(), true);
    if (row >= disk_cpu::expected.size())
      return;
    auto expected = disk_cpu::constants;
    for (unsigned n = 0; n < disk_cpu::variable_fields.size(); ++n)
      expected[disk_cpu::variable_fields[n]] = disk_cpu::expected[row][n];
    bool reported = false;
    for (unsigned n = 0; n < actual.size(); ++n) {
      if (!reported && actual[n] != expected[n]) {
        std::cerr << "CPU disk mode " << mode << ", pause " << pause << ", reset " << reset_kind
                  << ", phase " << phase << ", first difference in " << disk_cpu::names[n] << '\n';
        reported = true;
      }
      equal(actual[n], expected[n]);
    }
    ++checkpoints;
  }
};
} // namespace
void disk_cpu_tests() {
  Probe probe;
  disk_cpu::run(probe);
  equal(probe.checkpoints, 315);
}
} // namespace test
