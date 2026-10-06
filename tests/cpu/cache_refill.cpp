#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct RefillMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  bool wrote = false;
  bool fail_fill = false;
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
    address &= ~(bytes - 1u);
    auto &word = words[(address >> 2) % words.size()];
    if (bytes == 8) {
      word = static_cast<std::uint32_t>(value >> 32);
      words[((address >> 2) + 1) % words.size()] = static_cast<std::uint32_t>(value);
    } else {
      const auto shift = (4 - bytes - (address & 3)) * 8;
      const auto mask = bytes == 4 ? ~0u : ((1u << (bytes * 8)) - 1) << shift;
      word = (word & ~mask) | ((static_cast<std::uint32_t>(value) << shift) & mask);
    }
    return {clocks, true};
  }
};

struct RefillFixture {
  RefillMemory memory;
  Cpu cpu{memory};
  bool little;
  unsigned base;
  unsigned target;
  RefillFixture(bool little, unsigned base, unsigned word, bool cached)
      : little(little), base(base), target(base + 32 + word * 4) {
    code(base + 28, i(43, 1, 2, 0));
    code(target, i(9, 4, 4, 1));
    code(target + 4, c(4, 5, Status));
    configure(cached);
  }
  void code(unsigned address, std::uint32_t instruction) {
    memory.words[(address >> 2) ^ unsigned(little)] = instruction;
  }
  void configure(bool cached) {
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
    cpu.state().gpr[1] = 0xffffffffa0000000 | target;
    cpu.state().gpr[2] = i(9, 4, 4, 7);
    cpu.state().gpr[5] = 0x30000000;
    cpu.state().gpr[6] = little ? 0x7006e460 : 0x70066460;
    cpu.set_pc(0xffffffff80000000 | base);
    cpu.step();
    if (cached) {
      cpu.set_pc(0xffffffff80000000 | (base + 32));
      cpu.step();
      cpu.state().gpr[4] = 0;
    }
    cpu.set_pc(0xffffffff80000000 | (base + 28));
  }
};

void compare(RefillFixture &actual, RefillFixture &expected) {
  equal(actual.cpu.state().pc, expected.cpu.state().pc);
  equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
    equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
  equal(actual.memory.words == expected.memory.words, true);
}

void run(RefillFixture &actual, RefillFixture &expected, bool native, unsigned instructions) {
  for (unsigned n = 0; n < instructions; ++n)
    expected.cpu.step();
  for (unsigned n = 0; n < 64 && actual.cpu.state().pc != expected.cpu.state().pc; ++n) {
    const auto budget = actual.cpu.state().clocks;
    const bool handled =
        native ? actual.cpu.run_block(budget) : actual.cpu.run_interpreted_block(budget);
    if (!handled)
      actual.cpu.step();
  }
  compare(actual, expected);
}

} // namespace

void cache_refill_tests() {
  for (bool little : {false, true}) {
    for (bool native : {false, true}) {
      for (unsigned bytes : {1u, 2u, 8u}) {
        RefillFixture actual(little, 0x1000, 0, false);
        RefillFixture expected(little, 0x1000, 0, false);
        for (auto *fixture : {&actual, &expected}) {
          fixture->code(0x101c, i(bytes == 1 ? 40 : bytes == 2 ? 41 : 63, 1, 2, 0));
          fixture->cpu.state().gpr[1] += bytes < 4 && !little ? 4 - bytes : 0;
          const auto opcode = i(9, 4, 4, 7);
          const auto terminal = c(4, 5, Status);
          fixture->cpu.state().gpr[2] = bytes < 4 ? 7
                                        : little  ? (std::uint64_t(terminal) << 32) | opcode
                                                  : (std::uint64_t(opcode) << 32) | terminal;
          fixture->cpu.state().gpr[30] = 0xffffffff80001000;
          fixture->cpu.execute(i(47, 30, 16, 0));
          fixture->cpu.set_pc(0xffffffff8000101c);
        }
        run(actual, expected, native, 3);
        equal(actual.cpu.state().gpr[4], 7);
      }
      for (unsigned base : {0xfe0u, 0x1000u}) {
        for (unsigned word = 0; word < 8; ++word) {
          for (bool cached : {false, true}) {
            RefillFixture actual(little, base, word, cached);
            RefillFixture expected(little, base, word, cached);
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
              run(actual, expected, native, word + 3);
              if (!repeat)
                equal(actual.cpu.state().gpr[4], cached ? 1 : 7);
              for (auto *fixture : {&actual, &expected}) {
                fixture->cpu.state().gpr[2] = i(9, 4, 4, repeat & 1 ? 7 : 1);
                fixture->cpu.set_pc(0xffffffff80000000 | (base + 28));
              }
            }
            for (auto *fixture : {&actual, &expected}) {
              fixture->cpu.power();
              fixture->memory.wrote = false;
              fixture->configure(cached);
            }
            run(actual, expected, native, word + 3);
          }
        }
      }
      for (unsigned word : {0u, 1u, 7u}) {
        for (unsigned operation = 0; operation < 4; ++operation) {
          RefillFixture actual(little, 0x1000, word, false);
          RefillFixture expected(little, 0x1000, word, false);
          constexpr std::array<std::uint32_t, 4> replacements{r(12, 0, 0, 0), r(13, 0, 0, 0),
                                                              i(4, 0, 0, 2), c(4, 6, Config)};
          for (auto *fixture : {&actual, &expected})
            fixture->cpu.state().gpr[2] = replacements[operation];
          run(actual, expected, native, word + (operation == 2 ? 3 : 2));
          if (operation < 2) {
            equal(actual.cpu.read_control(Epc), 0xffffffff80000000 | actual.target);
            equal(actual.cpu.read_control(Cause) & 0x7c, (operation + 8) << 2);
          }
        }
      }
      RefillFixture actual(little, 0x1000, 1, false);
      RefillFixture expected(little, 0x1000, 1, false);
      actual.memory.fail_fill = expected.memory.fail_fill = true;
      run(actual, expected, native, 2);
      equal(actual.cpu.read_control(Epc), 0xffffffff80001020);
      equal(actual.cpu.read_control(Cause) & 0x7c, 6 << 2);
    }
  }
}

} // namespace test
