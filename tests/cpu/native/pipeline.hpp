#pragma once

#include "../native_memory_fixture.hpp"

namespace test::native_memory {

struct PipelineMemory : CachedMemory {
  struct Observation {
    std::uint64_t pc;
    std::uint32_t address;
    unsigned bytes;
  };
  Cpu *cpu = nullptr;
  std::vector<Observation> observations;
  std::uint32_t failed_burst = ~0u;
  std::uint32_t changed_burst = ~0u;

  BusRead read(std::uint32_t address, unsigned bytes) override {
    observations.push_back({cpu->state().pc, address, bytes});
    return CachedMemory::read(address, bytes);
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    observations.push_back({cpu->state().pc, address, bytes});
    return CachedMemory::write(address, bytes, value);
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    observations.push_back({cpu->state().pc, address, static_cast<unsigned>(output.size() * 4)});
    if (address == failed_burst) {
      transfers.push_back({address, static_cast<unsigned>(output.size() * 4), false, 0});
      return {clocks, false};
    }
    const auto result = CachedMemory::read_burst(address, output);
    if (address == changed_burst)
      output.front() = i(9, 0, 7, 0x55);
    return result;
  }
};

struct PipelineFixture : SizedCachedFixture<PipelineMemory> {
  PipelineFixture() {
    memory.cpu = &cpu;
  }
};

inline void pipeline_materialization_tests() {
  enum Access { Hit, Miss, Uncached, Misaligned, Failure };
  for (unsigned before : {2u, 7u, 8u, 15u}) {
    for (unsigned operation : {35u, 43u}) {
      for (const auto access : {Hit, Miss, Uncached, Misaligned, Failure}) {
        PipelineFixture actual;
        PipelineFixture expected;
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.state().gpr[1] = 0xffffffff80000200ull;
          fixture->cpu.state().gpr[2] = 0x12345678;
          fixture->cpu.state().gpr[31] = 0xffffffff80003000ull;
          for (unsigned n = 0; n < before; ++n)
            fixture->code(n * 4, i(9, 6, 6, 1));
          fixture->code(before * 4, i(operation, 1, 2, 0));
          fixture->code((before + 1) * 4, r(8, 31, 0, 0));
          fixture->code((before + 2) * 4, 0);
        }
        equal(actual.cpu.run_block(10000), true);
        equal(expected.cpu.run_interpreted_block(10000), true);
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.set_pc(0xffffffff80001000ull);
          fixture->cpu.state().gpr[6] = 0;
          fixture->cpu.state().gpr[2] = 0x12345678;
          fixture->cpu.state().gpr[1] = access == Hit          ? 0xffffffff80000200ull
                                        : access == Uncached   ? 0xffffffffa0000400ull
                                        : access == Misaligned ? 0xffffffff80000201ull
                                                               : 0xffffffff80000400ull;
          fixture->memory.failed_burst = access == Failure ? 0x400u : ~0u;
          fixture->memory.transfers.clear();
          fixture->memory.observations.clear();
          fixture->cpu.write_control(Count, 0);
          fixture->cpu.write_control(Compare, 0xffffffff);
        }
        const auto start = actual.cpu.state().clocks;
        const auto expected_start = expected.cpu.state().clocks;
        equal(actual.cpu.run_block(start + 10000), true);
        equal(expected.cpu.run_interpreted_block(expected_start + 10000), true);
        compare(actual, expected, false);
        if (access == Hit) {
          equal(actual.cpu.state().clocks - start, expected.cpu.state().clocks - expected_start);
          equal(actual.cpu.read_control(Count), expected.cpu.read_control(Count));
        }
        equal(actual.cpu.state().gpr[6], before);
        const auto memory_pc = 0xffffffff80001000ull + before * 4;
        if (access == Misaligned || access == Failure) {
          equal(actual.cpu.read_control(Epc), memory_pc);
          equal((actual.cpu.read_control(Cause) >> 2) & 31, access == Failure ? 7u
                                                            : operation == 35 ? 4u
                                                                              : 5u);
          equal(actual.cpu.read_control(Cause) >> 31, 0);
        }
        for (const auto &observation : actual.memory.observations)
          if (observation.address < 0x1000)
            equal(observation.pc, memory_pc);
        if (access == Miss || access == Uncached || access == Failure)
          equal(actual.memory.observations.empty(), false);
      }
    }
  }
  for (const bool failed : {false, true}) {
    PipelineFixture actual;
    PipelineFixture expected;
    for (auto *fixture : {&actual, &expected}) {
      for (unsigned n = 0; n < 8; ++n)
        fixture->code(n * 4, i(9, 6, 6, 1));
      fixture->code(32, 0);
      fixture->code(36, r(8, 31, 0, 0));
      fixture->code(40, 0);
      fixture->cpu.state().gpr[31] = 0xffffffff80003000ull;
      fixture->cpu.step();
      fixture->cpu.set_pc(0xffffffff80001000ull);
      fixture->cpu.state().gpr[6] = 0;
      fixture->memory.failed_burst = failed ? 0x1020u : ~0u;
      fixture->memory.changed_burst = failed ? ~0u : 0x1020u;
      fixture->memory.transfers.clear();
      fixture->memory.observations.clear();
    }
    equal(actual.cpu.run_block(actual.cpu.state().clocks + 10000), true);
    equal(expected.cpu.run_interpreted_block(expected.cpu.state().clocks + 10000), true);
    compare(actual, expected);
    equal(actual.cpu.state().gpr[6], 8);
    equal(actual.memory.observations.size(), 1);
    if (!actual.memory.observations.empty()) {
      equal(actual.memory.observations.front().address, 0x1020);
      equal(actual.memory.observations.front().pc, 0xffffffff80001020ull);
    }
    if (failed) {
      equal((actual.cpu.read_control(Cause) >> 2) & 31, 6);
      equal(actual.cpu.read_control(Epc), 0xffffffff80001020ull);
    } else {
      equal(actual.cpu.state().pc, 0xffffffff80001024ull);
      equal(actual.cpu.state().gpr[7], 0x55);
    }
  }
}

} // namespace test::native_memory
