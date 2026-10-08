#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct BoundaryMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  unsigned fills = 0;
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    ++fills;
    for (unsigned n = 0; n < output.size(); ++n)
      output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
};

struct BoundaryFixture {
  BoundaryMemory memory;
  Cpu cpu{memory};
  unsigned synchronizations = 0;
  BoundaryFixture(bool little, bool cached, unsigned program, unsigned parity) {
    memory.clocks = parity;
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
    cpu.state().gpr[4] = 0xffffffff80000000;
    const std::array code{i(9, 1, 1, 1), program == 1 ? i(35, 4, 2, 0) : i(13, 2, 2, 7),
                          i(program == 2 ? 20 : 4, 0, 0, 1), i(9, 3, 3, 1), c(4, 5, Status)};
    for (unsigned n = 0; n < code.size(); ++n)
      memory.words[(0x400 + n) ^ unsigned(little)] = code[n];
    memory.words[0] = 0x12345678;
    memory.words[1] = 0x87654321;
    if (cached) {
      cpu.set_pc(0xffffffff80001000);
      cpu.step();
    }
    if (program == 1)
      cpu.execute(i(35, 4, 2, 0));
    cpu.advance_clocks(parity);
    cpu.set_pc(0xffffffff80001000);
    cpu.connect_sync([this] { ++synchronizations; });
  }
};

void compare(BoundaryFixture &actual, BoundaryFixture &expected) {
  equal(actual.cpu.state().pc, expected.cpu.state().pc);
  equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  equal(actual.memory.fills, expected.memory.fills);
  equal(actual.synchronizations, expected.synchronizations);
  equal(actual.cpu.synchronization_limit(), expected.cpu.synchronization_limit());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
    equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
}

void count_registers() {
  for (bool cached : {false, true}) {
    for (auto initial : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
      for (bool writing : {false, true}) {
        for (bool load : {false, true}) {
          BoundaryMemory memory;
          Cpu cpu(memory);
          cpu.write_control(Status, 0x30000000);
          cpu.write_control(Compare, 0xffffffffu);
          std::vector<std::uint32_t> code;
          if (load)
            code.push_back(i(35, 4, 10, 0));
          code.insert(code.end(),
                      {c(writing ? 4u : 0u, 8, Count), c(0, 9, Count), r(8, 31, 0, 0), 0u});
          std::copy(code.begin(), code.end(), memory.words.begin() + 1024);
          cpu.state().gpr[4] = 0xffffffff80000000;
          cpu.set_pc(0xffffffff80001000);
          if (cached)
            cpu.step();
          cpu.set_pc(0xffffffff80001000);
          cpu.write_control(Count, initial);
          cpu.state().gpr[8] = initial;
          cpu.state().gpr[31] = 0xffffffff80002000;
          const auto start = cpu.state().clocks;
          const auto target = start + 4096;
          for (unsigned block = 0; block < 2 && cpu.state().pc != 0xffffffff80002000; ++block)
            equal(cpu.run_block(target), true);
          equal(cpu.state().pc, 0xffffffff80002000);
          const auto extra = cached ? (load ? 1u : 0u) : (load ? 45u : 24u);
          if (!writing)
            equal(cpu.state().gpr[8], sign_word(initial + extra));
          const auto second = extra;
          equal(cpu.state().gpr[9], sign_word(writing ? initial : initial + second));
          equal(cpu.read_control(Count), std::uint32_t(initial + (writing ? 2u : extra + 2u)));
          equal(cpu.state().clocks - start, cached ? (load ? 12 : 8) : (load ? 188 : 104));
        }
      }
    }
  }
}

void compare_interrupt() {
  for (bool cached : {false, true}) {
    for (auto initial : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
      BoundaryMemory memory;
      Cpu cpu(memory);
      cpu.write_control(Status, 0x30000000);
      cpu.write_control(Compare, 0xffffffffu);
      const std::array code{c(0, 9, Count), c(4, 8, Compare), r(8, 31, 0, 0), 0u};
      std::copy(code.begin(), code.end(), memory.words.begin() + 1024);
      cpu.set_pc(0xffffffff80001000);
      if (cached)
        cpu.step();
      cpu.set_pc(0xffffffff80001000);
      cpu.write_control(Count, initial);
      cpu.state().gpr[8] = std::uint32_t(initial + (cached ? 1u : 25u));
      cpu.state().gpr[31] = 0xffffffff80002000;
      const auto target = cpu.state().clocks + 4096;
      for (unsigned block = 0; block < 2 && cpu.state().pc != 0xffffffff80002000; ++block)
        equal(cpu.run_block(target), true);
      equal(cpu.state().pc, 0xffffffff80002000);
      equal(cpu.read_control(Cause) & 0x8000, 0);
      cpu.synchronize_timer();
      equal(cpu.read_control(Cause) & 0x8000, 0x8000);
    }
  }
}

} // namespace

void native_boundary_tests() {
  count_registers();
  compare_interrupt();
  for (bool little : {false, true}) {
    for (bool cached : {false, true}) {
      for (unsigned program : {0u, 1u, 2u}) {
        for (unsigned parity : {0u, 1u, 3u}) {
          for (unsigned count : {0u, 1u, 0x7fffffffu, 0xfffffffeu, 0xffffffffu}) {
            for (unsigned timer : {0u, 1u, 2u, 0x7fffffffu, 0xffffffffu}) {
              BoundaryFixture actual(little, cached, program, parity);
              BoundaryFixture expected(little, cached, program, parity);
              for (unsigned repeat = 0; repeat < 3; ++repeat) {
                for (auto *fixture : {&actual, &expected}) {
                  fixture->cpu.set_pc(0xffffffff80001000);
                  fixture->cpu.write_control(Count, count);
                  fixture->cpu.write_control(Compare, timer);
                }
                const auto target = actual.cpu.state().clocks;
                equal(actual.cpu.run_block(target), true);
                for (unsigned n = 0; n < 4; ++n)
                  expected.cpu.step();
                actual.cpu.synchronize_timer();
                expected.cpu.synchronize_timer();
                compare(actual, expected);
              }
            }
          }
        }
      }
    }
    BoundaryFixture actual(little, true, 0, 1);
    BoundaryFixture expected(little, true, 0, 1);
    for (auto *fixture : {&actual, &expected}) {
      fixture->cpu.write_control(Count, 100);
      fixture->cpu.write_control(Compare, 102);
      fixture->cpu.write_control(Status, 0x30008001);
    }
    const auto target = actual.cpu.state().clocks;
    equal(actual.cpu.run_block(target), true);
    for (unsigned n = 0; n < 4; ++n)
      expected.cpu.step();
    actual.cpu.synchronize_timer();
    expected.cpu.synchronize_timer();
    compare(actual, expected);
    equal(actual.synchronizations, 1);
    equal(actual.cpu.read_control(Cause) & 0x8000, 0x8000);
    equal(actual.cpu.run_block(target), true);
    equal(expected.cpu.run_interpreted_block(target), true);
    compare(actual, expected);
    equal(actual.cpu.read_control(Epc), 0xffffffff80001010);
  }
}

} // namespace test
