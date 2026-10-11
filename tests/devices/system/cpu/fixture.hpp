#pragma once
#include "../../fixture.hpp"
#include "core/system/console.hpp"
#include "program.hpp"
#include <span>

namespace test::cpu_bus {
using namespace cupid::n64;
struct Machine {
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;
  unsigned block = 0;
  std::unique_ptr<Console> console;
  bool native = false;
  void word(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n)
      hash = (hash ^ ((value >> (n * 8)) & 255)) * 0x100000001b3ull;
    ++observations;
  }
  void finish(std::span<const std::uint64_t> expected, const char *label) {
    equal(block < expected.size(), true);
    if (block < expected.size()) {
      if (hash != expected[block])
        std::cerr << label << " scenario " << block << '\n';
      equal(hash, expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
  void create(ConsoleConfig config, bool compiled) {
    native = compiled;
    config.random_seed = 0;
    console = std::make_unique<Console>(config);
  }
  void load_rom() {
    std::array<std::uint8_t, 0x1000> rom{};
    std::array<std::uint8_t, 0x7c0> firmware{};
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    if (!console->load(rom, firmware))
      throw std::runtime_error("CPU bus ROM load failed");
  }
  void initial_write(unsigned address, unsigned value) {
    console->write(address, 4, value);
  }
  void install() {
    for (unsigned n = 0; n < 3; ++n) {
      console->ram().write(cpu_bus::WriteCode + n * 4, 4, cpu_bus::WriteWords[n]);
      console->ram().write(cpu_bus::ReadCode + n * 4, 4, cpu_bus::ReadWords[n]);
    }
    console->cpu().write_control(Status, 0x30000000);
  }
  void fill_ram() {
    for (unsigned n = 0; n < cpu_bus::RamBytes; ++n)
      console->ram().write(n, 1, cpu_bus::pattern(n, 0x53));
  }
  auto access(bool write, unsigned address, unsigned value) {
    auto &cpu = console->cpu();
    cpu.set_pc(0xffffffff80000000ull + (write ? cpu_bus::WriteCode : cpu_bus::ReadCode));
    cpu.state().gpr[1] = 0xffffffffa0000000ull | address;
    cpu.state().gpr[2] = static_cast<std::int64_t>(static_cast<std::int32_t>(value));
    cpu.state().gpr[31] = 0xffffffff80000000ull + cpu_bus::Stop;
    for (unsigned calls = 0; cpu.state().pc != cpu.state().gpr[31]; ++calls) {
      if (calls == 16)
        throw std::runtime_error("CPU bus instruction stream did not finish");
      if (native) {
        if (!cpu.run_block(cpu.state().clocks + 1))
          throw std::runtime_error("CPU bus native block unavailable");
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
  unsigned deadline() {
    return static_cast<unsigned>(console->next_event_clocks());
  }
};
} // namespace test::cpu_bus
