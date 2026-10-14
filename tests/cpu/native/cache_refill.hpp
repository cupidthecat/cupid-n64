#pragma once

#include "../native_memory_fixture.hpp"
#include "core/memory/instruction_tracker.hpp"

namespace test::native_memory {

struct DirectRefillMemory : CachedMemory {
  InstructionTracker tracker{sizeof(words)};
  std::array<std::uint32_t, 2048> replacement{};
  bool replaced = false, truncated = false;
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    const auto &backing = replaced ? replacement : words;
    return address < sizeof(backing) ? std::span(backing).subspan(address >> 2)
                                     : std::span<const std::uint32_t>();
  }
  std::span<const std::uint32_t> cache_fill_data(std::uint32_t address) const override {
    auto data = instruction_data(address);
    return truncated ? data.first(std::min<std::size_t>(7, data.size())) : data;
  }
  InstructionTracker *instruction_tracker() override {
    return &tracker;
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    const auto data = instruction_data(address);
    if (data.size() < output.size())
      return {0, false};
    std::copy_n(data.begin(), output.size(), output.begin());
    return {};
  }
};

inline void direct_refill() {
  for (unsigned first : {0u, 1u, 6u, 7u}) {
    for (bool truncated : {false, true}) {
      SizedCachedFixture<DirectRefillMemory> actual, expected;
      for (auto *f : {&actual, &expected}) {
        f->memory.truncated = truncated;
        f->code(first * 4, i(9, 0, 4, 17));
        f->code((first + 1) * 4, i(9, 4, 5, 23));
        f->code((first + 2) * 4, r(8, 31, 0, 0));
        f->code((first + 3) * 4, i(9, 5, 6, 31));
        f->cpu.state().gpr[31] = 0xffffffff80003000ull;
      }
      for (unsigned repeat = 0; repeat < 3; ++repeat) {
        for (auto *f : {&actual, &expected}) {
          if (repeat == 1) {
            f->memory.replacement = f->memory.words;
            f->memory.replaced = true;
            f->memory.tracker.invalidate_all();
          }
          if (repeat == 2)
            f->memory.tracker.set_fully_tracked(false);
          for (unsigned line = 0; line < 2; ++line) {
            f->cpu.state().gpr[1] = 0xffffffff80001000ull + line * 32;
            f->cpu.execute(i(47, 1, 0, 0));
          }
          f->cpu.set_pc(0xffffffff80001000ull + first * 4);
        }
        const auto start = actual.cpu.state().clocks;
        equal(actual.cpu.run_block(start + 10000), true);
        equal(expected.cpu.run_interpreted_block(expected.cpu.state().clocks + 10000), true);
        equal(actual.cpu.state().clocks - start, first >= 6 ? 200 : 104);
        equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
        equal(actual.cpu.state().pc, expected.cpu.state().pc);
        equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
        for (unsigned reg = 0; reg < 32; ++reg) {
          equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
          equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
        }
        equal(actual.cpu.state().gpr[4], 17);
        equal(actual.cpu.state().gpr[5], 40);
        equal(actual.cpu.state().gpr[6], 71);
      }
    }
  }
}

} // namespace test::native_memory
