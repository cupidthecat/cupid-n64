#pragma once

#include "../support/test.hpp"
#include <algorithm>

namespace test::native_memory {
using namespace cupid::n64;

struct CachedMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    transfers.push_back({address, static_cast<unsigned>(output.size() * 4), false, 0});
    if (success)
      for (unsigned n = 0; n < output.size(); ++n)
        output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> input) override {
    for (unsigned n = 0; n < input.size(); ++n) {
      transfers.push_back({address + n * 4, 4, true, input[n]});
      if (success)
        words[((address >> 2) + n) % words.size()] = input[n];
    }
    return {clocks, success};
  }
};

struct CachedFixture {
  CachedMemory memory;
  Cpu cpu{memory};
  CachedFixture() {
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(0xffffffff80001000);
    for (unsigned n = 0; n < 1024; ++n)
      memory.words[n] = 0x8091a2b3 ^ (n * 0x172345);
  }
  void code(unsigned offset, std::uint32_t instruction, bool little) {
    memory.words[(1024 + offset / 4) ^ unsigned(little)] = instruction;
  }
};

inline void compare(CachedFixture &actual, CachedFixture &expected, bool timing = true) {
  const auto &a = actual.cpu.state();
  const auto &b = expected.cpu.state();
  equal(a.pc, b.pc);
  if (timing)
    equal(a.clocks, b.clocks);
  equal(a.hi, b.hi);
  equal(a.lo, b.lo);
  equal(a.fcr31, b.fcr31);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(a.gpr[reg], b.gpr[reg]);
    equal(a.fpr[reg], b.fpr[reg]);
    if (timing || reg != Count)
      equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
  equal(actual.memory.transfers.size(), expected.memory.transfers.size());
  for (unsigned n = 0;
       n < std::min(actual.memory.transfers.size(), expected.memory.transfers.size()); ++n) {
    const auto &x = actual.memory.transfers[n];
    const auto &y = expected.memory.transfers[n];
    equal(x.address, y.address);
    equal(x.bytes, y.bytes);
    equal(x.store, y.store);
    equal(x.value, y.value);
  }
  for (unsigned n = 0; n < actual.memory.words.size(); ++n)
    equal(actual.memory.words[n], expected.memory.words[n]);
}

} // namespace test::native_memory
