#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct EntryMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  bool stopped = false;
  bool frozen() const override {
    return stopped;
  }
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    if (success)
      for (unsigned n = 0; n < output.size(); ++n)
        output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
};

struct EntryFixture {
  EntryMemory memory;
  Cpu cpu{memory};
  bool little;
  explicit EntryFixture(bool little) : little(little) {
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
    cpu.set_pc(0xffffffff80001000);
  }
  void code(unsigned address, std::uint32_t instruction) {
    memory.words[(address >> 2) ^ unsigned(little)] = instruction;
  }
  void program(bool likely) {
    code(0x1000, i(9, 3, 3, 1));
    code(0x1004, 0x0c000600);
    code(0x1008, i(9, 4, 4, 1));
    code(0x100c, i(9, 5, 5, 1));
    code(0x1010, i(likely ? 20u : 4u, 1, 0, 2));
    code(0x1014, i(9, 6, 6, 1));
    code(0x1018, i(9, 7, 7, 1));
    code(0x101c, i(9, 8, 8, 1));
    code(0x1020, 0x08000800);
    code(0x1024, i(9, 9, 9, 1));
    code(0x1800, i(9, 10, 10, 1));
    code(0x1804, r(8, 31, 0, 0));
    code(0x1808, i(9, 11, 11, 1));
  }
};

void compare(EntryFixture &actual, EntryFixture &expected) {
  equal(actual.cpu.state().pc, expected.cpu.state().pc);
  equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  equal(actual.memory.frozen(), expected.memory.frozen());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
    equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
}

void run(EntryFixture &actual, EntryFixture &expected) {
  const auto budget = actual.cpu.state().clocks;
  equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
  compare(actual, expected);
}

} // namespace

void native_entry_tests() {
  for (bool little : {false, true}) {
    for (unsigned action = 0; action < 5; ++action) {
      EntryFixture actual(little);
      EntryFixture expected(little);
      for (auto *fixture : {&actual, &expected})
        fixture->program(false);
      run(actual, expected);
      run(actual, expected);
      equal(actual.cpu.state().pc, 0xffffffff8000100c);
      const auto before = actual.cpu.state().gpr[5];
      for (auto *fixture : {&actual, &expected}) {
        if (action == 0) {
          fixture->memory.stopped = true;
        } else if (action == 1) {
          fixture->cpu.request_nmi();
        } else if (action == 2) {
          fixture->cpu.write_control(Status, 0x30000401);
          fixture->cpu.set_interrupt(2, true);
        } else if (action == 3) {
          fixture->cpu.state().gpr[30] = 0xffffffff80001000;
          fixture->cpu.execute(i(47, 30, 16, 0));
          fixture->cpu.set_pc(0xffffffff8000100c);
          fixture->memory.success = false;
        } else {
          fixture->cpu.write_control(Status, 0x30008001);
          fixture->cpu.write_control(Count, 0);
          fixture->cpu.write_control(Compare, 1);
        }
      }
      run(actual, expected);
      equal(actual.cpu.state().gpr[5], before + (action == 4));
      if (action == 1)
        equal(actual.cpu.read_control(ErrorEpc), 0xffffffff8000100c);
      if (action == 2 || action == 3) {
        equal(actual.cpu.read_control(Epc), 0xffffffff8000100c);
        equal(actual.cpu.read_control(Cause) & 0x7c, action == 2 ? 0 : 6 << 2);
      }
      if (action == 4) {
        actual.cpu.synchronize_timer();
        expected.cpu.synchronize_timer();
        equal(actual.cpu.read_control(Cause) & 0x8000, 0x8000);
        run(actual, expected);
        equal(actual.cpu.read_control(Epc), 0xffffffff8000101c);
      }
    }
    for (bool existing_entry : {false, true}) {
      EntryFixture actual(little);
      EntryFixture expected(little);
      for (auto *fixture : {&actual, &expected}) {
        fixture->code(0x1000, i(9, 3, 3, 1));
        fixture->code(0x1004, i(4, 1, 0, 14));
        fixture->code(0x1008, i(9, 4, 4, 1));
        fixture->code(0x100c, i(9, 5, 5, 1));
        fixture->code(0x1040, i(9, 8, 8, 1));
        fixture->code(0x1044, 0x08000800);
        fixture->code(0x1048, i(9, 9, 9, 1));
        fixture->cpu.state().gpr[1] = 1;
        if (existing_entry)
          fixture->cpu.set_pc(0xffffffff80001040);
      }
      if (existing_entry) {
        run(actual, expected);
        for (auto *fixture : {&actual, &expected})
          fixture->cpu.set_pc(0xffffffff80001000);
      }
      run(actual, expected);
      equal(actual.cpu.state().pc, 0xffffffff8000100c);
      run(actual, expected);
      for (auto *fixture : {&actual, &expected}) {
        fixture->code(0x1000, i(9, 3, 3, 7));
        fixture->cpu.set_pc(0xffffffff80001040);
      }
      const auto prefix_result = actual.cpu.state().gpr[3];
      run(actual, expected);
      equal(actual.cpu.state().gpr[3], prefix_result);
      for (auto *fixture : {&actual, &expected})
        fixture->cpu.set_pc(0xffffffff80001000);
      const auto budget = actual.cpu.state().clocks;
      equal(actual.cpu.run_block(budget), false);
      equal(expected.cpu.run_interpreted_block(budget), false);
      compare(actual, expected);
      for (auto *fixture : {&actual, &expected}) {
        fixture->cpu.state().gpr[30] = 0xffffffff80001000;
        fixture->cpu.execute(i(47, 30, 16, 0));
        fixture->cpu.set_pc(0xffffffff80001000);
      }
      run(actual, expected);
      equal(actual.cpu.state().gpr[3], prefix_result + 7);
      run(actual, expected);
      for (auto *fixture : {&actual, &expected}) {
        fixture->code(0x1040, i(9, 8, 8, 7));
        fixture->cpu.state().gpr[30] = 0xffffffff80001040;
        fixture->cpu.execute(i(47, 30, 16, 0));
        fixture->cpu.set_pc(0xffffffff80001040);
      }
      run(actual, expected);
      for (auto *fixture : {&actual, &expected})
        fixture->cpu.set_pc(0xffffffff80001000);
      run(actual, expected);
      run(actual, expected);
    }
    for (bool likely : {false, true}) {
      for (bool taken : {false, true}) {
        EntryFixture actual(little);
        EntryFixture expected(little);
        for (auto *fixture : {&actual, &expected}) {
          fixture->program(likely);
          fixture->cpu.state().gpr[1] = taken ? 0 : 1;
        }
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
          run(actual, expected);
          equal(actual.cpu.state().pc, 0xffffffff80001800);
          run(actual, expected);
          equal(actual.cpu.state().pc, 0xffffffff8000100c);
          run(actual, expected);
          equal(actual.cpu.state().pc, taken ? 0xffffffff8000101c : 0xffffffff80001018);
          run(actual, expected);
          equal(actual.cpu.state().pc, 0xffffffff80002000);
          for (auto *fixture : {&actual, &expected})
            fixture->cpu.set_pc(0xffffffff80001000);
        }
        for (auto *fixture : {&actual, &expected}) {
          fixture->code(0x101c, i(9, 8, 8, 7));
          fixture->cpu.set_pc(0xffffffff8000101c);
        }
        const auto budget = actual.cpu.state().clocks;
        const auto cached_result = actual.cpu.state().gpr[8];
        for (unsigned n = 0; n < 3; ++n)
          expected.cpu.step();
        for (unsigned n = 0; n < 8 && actual.cpu.state().pc != expected.cpu.state().pc; ++n)
          if (!actual.cpu.run_block(budget))
            actual.cpu.step();
        compare(actual, expected);
        equal(actual.cpu.state().gpr[8], cached_result + 1);
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.state().gpr[30] = 0xffffffff80001000;
          fixture->cpu.execute(i(47, 30, 16, 0));
          fixture->cpu.set_pc(0xffffffff8000101c);
        }
        run(actual, expected);
        for (auto *fixture : {&actual, &expected})
          fixture->cpu.set_pc(0xffffffff80001000);
        run(actual, expected);
        run(actual, expected);
        run(actual, expected);
        run(actual, expected);
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.set_pc(0xffffffff80001008);
          fixture->cpu.execute(0x08000800);
        }
        run(actual, expected);
        equal(actual.cpu.state().pc, 0xffffffff80002000);
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.power();
          fixture->cpu.write_control(Status, 0x30000000);
          fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
          fixture->cpu.set_pc(0xffffffff80001000);
        }
        run(actual, expected);
        run(actual, expected);
        run(actual, expected);
        run(actual, expected);
      }
    }
  }
}

} // namespace test
