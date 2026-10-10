#pragma once

#include "../../fixture.hpp"
#include "core/system/console.hpp"
#include "immediate/scenarios.hpp"
#include <span>
#include <stdexcept>

namespace test::reset_fixture {
using namespace cupid::n64;

struct Machine {
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;
  unsigned block = 0;
  std::unique_ptr<Console> console;
  bool expanded = false;
  void reset(bool expansion) {
    expanded = expansion;
    ConsoleConfig config;
    config.expansion = expanded;
    config.random_seed = 0;
    config.eeprom_size = 512;
    config.sram_size = 32768;
    console = std::make_unique<Console>(config);
    std::array<std::uint8_t, 0x7c0> firmware{};
    if (!console->load(test::machine_reset::rom(), firmware))
      throw std::runtime_error("Machine reset load failed");
  }
  bool expansion() const {
    return expanded;
  }
  void write(unsigned address, unsigned value) {
    console->write(address, 4, value);
  }
  unsigned read(unsigned address) {
    return static_cast<unsigned>(console->read(address, 4).value);
  }
  void fill(unsigned seed) {
    if (!console->ram().identity())
      throw std::runtime_error("Machine reset RAM mapping initialization failed");
    for (unsigned n = 0; n < test::machine_reset::MemoryBytes; ++n)
      console->ram().write(n, 1, test::machine_reset::pattern(n, seed));
    for (unsigned n = 0; n < 8192; ++n)
      console->signal().write_local(n, 1, test::machine_reset::pattern(n, seed + 1));
    for (unsigned n = 0; n < 512; ++n) {
      console->eeprom().data()[n] = test::machine_reset::pattern(n, seed + 2);
      console->sram().data()[n] = test::machine_reset::pattern(n, seed + 3);
    }
    for (unsigned n = 0; n < 64; ++n)
      console->pif().ram()[n] = test::machine_reset::pattern(n, seed + 4);
  }
  void dirty_cpu(unsigned seed) {
    for (unsigned n = 1; n < 32; ++n)
      console->cpu().state().gpr[n] = 0x1234567800000000ull + n * 13 + seed;
    console->cpu().set_pc(0xffffffffa4000020ull);
    console->cpu().write_control(Count, 0x1234);
    console->cpu().write_control(Compare, 0x2345);
    console->cpu().write_control(Status, 0x3000ff01);
    console->cpu().state().gpr[1] = 0xffffffff80006000ull;
    console->cpu().state().gpr[2] = 0xffffffff9123abcdull;
    console->cpu().execute(35u << 26 | 1u << 21 | 3u << 16);
    console->cpu().execute(43u << 26 | 1u << 21 | 2u << 16 | 4u);
    console->cpu().execute(47u << 26 | 1u << 21 | 20u << 16);
    console->cpu().execute(48u << 26 | 1u << 21 | 3u << 16);
    console->cpu().write_control(Index, 3);
    console->cpu().write_control(PageMask, 0x6000);
    console->cpu().write_control(EntryHi, 0x0040005a);
    console->cpu().write_control(EntryLo0, 0x19f);
    console->cpu().write_control(EntryLo1, 0x25f);
    console->cpu().execute(0x42000002);
    for (unsigned n = 1; n < 32; ++n)
      console->cpu().state().gpr[n] = 0x1234567800000000ull + n * 13 + seed;
    console->cpu().set_pc(0xffffffffa400001cull);
    console->cpu().execute(4u << 26 | 3u);
    for (unsigned n = 0; n < 32; ++n)
      console->cpu().state().fpr[n] = 0x9abcdef000000000ull + n * 17 + seed;
    console->cpu().state().hi = 0x0123456789abcdefull;
    console->cpu().state().lo = 0xfedcba9876543210ull;
    auto &rsp = console->signal().state();
    for (unsigned n = 0; n < 32; ++n) {
      rsp.gpr[n] = n ? n * 31 + seed : 0;
      for (unsigned lane = 0; lane < 8; ++lane)
        rsp.vectors[n].lanes[lane] = n * 127 + lane * 43 + seed;
    }
    for (unsigned lane = 0; lane < 8; ++lane)
      rsp.accumulator.set(lane, 0x123456789abcull + lane + seed);
    rsp.carry_low = 0x69;
    rsp.carry_high = 0xa5;
    rsp.compare_low = 0x3c;
    rsp.compare_high = 0xc3;
    rsp.extension = 0x5a;
    rsp.divide_input = 0x9876;
    rsp.divide_output = 0x5432;
    rsp.divide_double = true;
  }
  void start_eeprom(unsigned seed) {
    std::array<std::uint8_t, 10> input{5, 7};
    std::array<std::uint8_t, 1> output{};
    for (unsigned n = 2; n < input.size(); ++n)
      input[n] = test::machine_reset::pattern(n, seed + 5);
    console->eeprom().communicate(input, output);
  }
  void request_nmi() {
    console->cpu().request_nmi();
  }
  unsigned eeprom_status() {
    std::array<std::uint8_t, 1> input{0};
    std::array<std::uint8_t, 3> output{};
    console->eeprom().communicate(input, output);
    return (unsigned(output[0]) << 16) | (unsigned(output[1]) << 8) | output[2];
  }
  void power(bool warm) {
    console->power(warm);
  }
  void word(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
      hash ^= (value >> (n * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish(std::span<const std::uint64_t> expected, const char *label) {
    test::equal(block < expected.size(), true);
    if (block < expected.size()) {
      if (hash != expected[block])
        std::cerr << label << " scenario " << block << '\n';
      test::equal(hash, expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
  std::uint64_t pc() {
    return console->cpu().state().pc;
  }
  std::uint64_t cpu_clock() {
    return console->cpu().state().clocks;
  }
  std::uint64_t gpr(unsigned n) {
    return console->cpu().state().gpr[n];
  }
  std::uint64_t control(unsigned n) {
    return console->cpu().read_control(n);
  }
  bool identity() {
    return console->ram().identity();
  }
  bool frozen() {
    return console->frozen();
  }
  unsigned pif_state() {
    return static_cast<unsigned>(console->pif().state());
  }
  bool pif_reset() {
    return console->pif().reset_enabled();
  }
  std::uint64_t pif_memory(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | console->pif().ram()[address + n];
    return value;
  }
  std::uint64_t raw_memory(unsigned address) {
    const auto memory = static_cast<const Rdram &>(console->ram()).words();
    return (std::uint64_t(memory[address / 4]) << 32) | memory[address / 4 + 1];
  }
  std::uint64_t rsp_memory(unsigned address) {
    return console->signal().read_local(address, 8);
  }
  std::uint64_t eeprom_memory(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | console->eeprom().data()[address + n];
    return value;
  }
  std::uint64_t sram_memory(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | console->sram().data()[address + n];
    return value;
  }
  void internal();
};
inline void Machine::internal() {
  auto &cpu = console->cpu();
  word(cpu.in_delay_slot());
  for (const auto value : cpu.state().fpr)
    word(value);
  word(cpu.state().fcr31);
  word(cpu.state().hi);
  word(cpu.state().lo);
  auto &rsp = console->signal();
  word(rsp.pc());
  word(static_cast<std::uint64_t>(rsp.clocks()));
  word(static_cast<std::uint64_t>(rsp.dma_clocks()));
  word(rsp.dma_busy());
  for (const auto value : rsp.state().gpr)
    word(value);
  for (const auto &reg : rsp.state().vectors)
    for (const auto value : reg.lanes)
      word(value);
  for (unsigned lane = 0; lane < 8; ++lane)
    word(rsp.state().accumulator.get(lane));
  word(rsp.state().carry_low);
  word(rsp.state().carry_high);
  word(rsp.state().compare_low);
  word(rsp.state().compare_high);
  word(rsp.state().extension);
  word(rsp.state().divide_input);
  word(rsp.state().divide_output);
  word(rsp.state().divide_double);
}

} // namespace test::reset_fixture
