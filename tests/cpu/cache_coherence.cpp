#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct CoherenceMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  bool wrote = false;
  bool fail_fill = false;
  bool fail_store = false;
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    if (wrote && fail_fill)
      return {clocks, false};
    for (unsigned n = 0; n < output.size(); ++n)
      output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, true};
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    wrote = true;
    if (fail_store)
      return {clocks, false};
    if (bytes == 4)
      words[(address >> 2) % words.size()] = static_cast<std::uint32_t>(value);
    return {clocks, true};
  }
};

struct CoherenceFixture {
  CoherenceMemory memory;
  Cpu cpu{memory};
  bool little;
  explicit CoherenceFixture(bool little, unsigned operation, bool branch_delay) : little(little) {
    code(0x1000, i(43, 1, 2, 0));
    code(0x1004, branch_delay ? i(4, 0, 0, 1) : i(47, 3, operation, 0));
    code(0x1008, branch_delay ? i(47, 3, operation, 0) : i(9, 4, 4, 1));
    code(0x100c, branch_delay ? i(9, 4, 4, 1) : c(4, 5, Status));
    if (branch_delay)
      code(0x1010, c(4, 5, Status));
    configure(branch_delay);
  }
  void code(unsigned address, std::uint32_t instruction) {
    memory.words[(address >> 2) ^ unsigned(little)] = instruction;
  }
  void configure(bool branch_delay) {
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
    cpu.state().gpr[1] = branch_delay ? 0xffffffffa000100c : 0xffffffffa0001008;
    cpu.state().gpr[2] = i(9, 4, 4, 7);
    cpu.state().gpr[3] = 0xffffffff80001000;
    cpu.state().gpr[5] = 0x30000000;
    cpu.set_pc(0xffffffff80001014);
    cpu.step();
    cpu.set_pc(0xffffffff80001000);
  }
};

void compare(CoherenceFixture &actual, CoherenceFixture &expected) {
  equal(actual.cpu.state().pc, expected.cpu.state().pc);
  equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
    equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
  equal(actual.memory.words == expected.memory.words, true);
}

void run(CoherenceFixture &actual, CoherenceFixture &expected, bool native, unsigned instructions) {
  for (unsigned n = 0; n < instructions; ++n)
    expected.cpu.step();
  for (unsigned n = 0; n < 32 && actual.cpu.state().pc != expected.cpu.state().pc; ++n) {
    const auto budget = actual.cpu.state().clocks;
    const bool handled =
        native ? actual.cpu.run_block(budget) : actual.cpu.run_interpreted_block(budget);
    if (!handled)
      actual.cpu.step();
  }
  compare(actual, expected);
}

} // namespace

void cache_coherence_tests() {
  for (bool little : {false, true}) {
    for (bool native : {false, true}) {
      for (bool branch_delay : {false, true}) {
        for (unsigned operation : {0u, 8u, 16u, 17u, 20u, 24u, 31u}) {
          CoherenceFixture actual(little, operation, branch_delay);
          CoherenceFixture expected(little, operation, branch_delay);
          const unsigned count = branch_delay ? 5 : 4;
          run(actual, expected, native, count);
          equal(actual.cpu.state().gpr[4],
                operation == 17 || operation == 24 || operation == 31 ? 1 : 7);
          for (auto *fixture : {&actual, &expected}) {
            fixture->cpu.state().gpr[2] = i(9, 4, 4, 1);
            fixture->cpu.set_pc(0xffffffff80001000);
          }
          run(actual, expected, native, count);
          for (auto *fixture : {&actual, &expected}) {
            fixture->cpu.power();
            fixture->memory.wrote = false;
            fixture->configure(branch_delay);
          }
          run(actual, expected, native, count);
        }
      }
      for (bool store_fault : {false, true}) {
        CoherenceFixture actual(little, 20, false);
        CoherenceFixture expected(little, 20, false);
        for (auto *fixture : {&actual, &expected}) {
          fixture->memory.fail_store = store_fault;
          fixture->memory.fail_fill = !store_fault;
        }
        run(actual, expected, native, store_fault ? 1 : 2);
        equal(actual.cpu.read_control(Epc), store_fault ? 0xffffffff80001000 : 0xffffffff80001004);
        equal(actual.cpu.read_control(Cause) & 0x7c, (store_fault ? 7 : 6) << 2);
      }
    }
  }
}

} // namespace test
