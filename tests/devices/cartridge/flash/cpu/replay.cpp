#include "../../../fixture.hpp"
#include "core/system/console.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
namespace test {
using namespace cupid::n64;
namespace {
struct Probe {
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;
  unsigned block = 0;
  std::unique_ptr<Console> console;
  bool native = false;
  void reset(unsigned model, bool compiled) {
    native = compiled;
    ConsoleConfig config;
    config.random_seed = 0;
    config.flash_model = static_cast<FlashModel>(model);
    console = std::make_unique<Console>(config);
    std::array<std::uint8_t, 0x1000> rom{};
    std::array<std::uint8_t, 0x7c0> firmware{};
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    if (!console->load(rom, firmware))
      throw std::runtime_error("CPU Flash ROM load failed");
  }
  void initial_write(unsigned address, unsigned value) {
    console->write(address, 4, value);
  }
  void install() {
    for (unsigned n = 0; n < 3; ++n) {
      console->ram().write(cpu_flash::WriteCode + n * 4, 4, cpu_flash::write_code[n]);
      console->ram().write(cpu_flash::ReadCode + n * 4, 4, cpu_flash::read_code[n]);
    }
    console->cpu().write_control(Status, 0x30000000);
  }
  void fill() {
    for (unsigned n = 0; n < cpu_flash::RamBytes; ++n)
      console->ram().write(n, 1, cpu_flash::pattern(n, 0x53));
    for (unsigned n = 0; n < cpu_flash::FlashBytes; ++n)
      console->flash().data()[n] = cpu_flash::pattern(n, 0x29);
  }
  std::uint64_t access(bool write, unsigned address, unsigned value) {
    auto &cpu = console->cpu();
    cpu.set_pc(0xffffffff80000000ull + (write ? cpu_flash::WriteCode : cpu_flash::ReadCode));
    cpu.state().gpr[1] = 0xffffffffa0000000ull | address;
    cpu.state().gpr[2] =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(value)));
    cpu.state().gpr[31] = 0xffffffff80000000ull + cpu_flash::Stop;
    for (unsigned calls = 0; cpu.state().pc != cpu.state().gpr[31]; ++calls) {
      if (calls == 16)
        throw std::runtime_error("CPU Flash instruction stream did not finish");
      if (native) {
        const auto target = cpu.state().clocks + 1;
        if (!cpu.run_block(target))
          throw std::runtime_error("CPU Flash native block unavailable");
        console->synchronize();
      } else
        console->step();
    }
    return cpu.state().gpr[2];
  }
  void idle(unsigned clocks) {
    console->cpu().advance_clocks(clocks);
    console->synchronize();
  }
  void power(bool warm) {
    console->power(warm);
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
  bool frozen() {
    return console->frozen();
  }
  auto register_word(unsigned address) {
    return console->read(address, 4).value;
  }
  auto ram_word(unsigned address) {
    return console->ram().read(address, 8);
  }
  std::uint64_t coverage_word(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = value << 8 | console->ram().hidden()[address + n];
    return value;
  }
  std::uint64_t flash_word(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = value << 8 | console->flash().data()[address + n];
    return value;
  }
  void word(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
      hash ^= (value >> (n * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    equal(block < cpu_flash::expected.size(), true);
    if (block < cpu_flash::expected.size()) {
      if (hash != cpu_flash::expected[block])
        std::cerr << "CPU Flash scenario " << block << '\n';
      equal(hash, cpu_flash::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace
void flash_cpu_tests() {
  Probe probe;
  cpu_flash::scenarios(probe);
  equal(probe.block, cpu_flash::expected.size());
  equal(probe.observations, 98745360);
}
} // namespace test
