#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct BranchMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    for (unsigned n = 0; n < output.size(); ++n)
      output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
};

struct BranchFixture {
  BranchMemory memory;
  Cpu cpu{memory};
  BranchFixture() {
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(0xffffffff80001000);
  }
  void code(unsigned word, std::uint32_t instruction, bool little) {
    memory.words[(1024 + word) ^ unsigned(little)] = instruction;
  }
};

} // namespace

void native_branch_tests() {
  constexpr std::array<std::uint64_t, 8> values{
      0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x7fffffffffffffff, 0x8000000000000000, ~0ull};
  constexpr std::array<unsigned, 4> registers{0, 1, 2, 31};
  for (bool little : {false, true}) {
    for (unsigned family = 0; family < 20; ++family) {
      for (auto rs : registers) {
        for (auto rt : registers) {
          for (auto value : values) {
            for (unsigned prefix : {0u, 3u, 7u}) {
              for (bool nested : {false, true}) {
                BranchFixture actual;
                BranchFixture expected;
                for (auto *fixture : {&actual, &expected}) {
                  fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
                  fixture->cpu.state().gpr[rs] = value;
                  fixture->cpu.state().gpr[rt] = value ^ 0x80000000;
                  std::uint32_t instruction;
                  if (family < 8) {
                    instruction = i(family < 4 ? family + 4 : family + 16, rs, rt, 0xfffc);
                  } else if (family < 16) {
                    instruction = i(1, rs, family < 12 ? family - 8 : family + 4, 0xfffc);
                  } else if (family < 18) {
                    instruction = (family == 16 ? 2u : 3u) << 26 | 0x800;
                  } else {
                    instruction = r(family == 18 ? 8u : 9u, rs, 0, rt);
                  }
                  for (unsigned word = 0; word < prefix; ++word)
                    fixture->code(word, 0, little);
                  if (nested) {
                    fixture->code(prefix, 0x08000800, little);
                    fixture->code(prefix + 1, instruction, little);
                  } else {
                    fixture->code(prefix, instruction, little);
                    fixture->code(prefix + 1, i(9, 3, 3, 1), little);
                  }
                  fixture->code(prefix + 2, i(9, 0, 28, 7), little);
                }
                const std::uint64_t budget = 0;
                equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
                equal(actual.cpu.state().pc, expected.cpu.state().pc);
                equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
                equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
                for (unsigned reg = 0; reg < 32; ++reg) {
                  equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
                  equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
                }
              }
            }
          }
        }
      }
    }
  }
}

} // namespace test
