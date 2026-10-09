#include "../support/test.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;

namespace {
class RandomMemory : public Memory {
public:
  void instruction(unsigned index, std::uint32_t value) {
    words_[index] = value;
    put(index * 4, 4, value);
  }

  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address == 0 ? std::span<const std::uint32_t>(words_) : std::span<const std::uint32_t>{};
  }

private:
  std::array<std::uint32_t, 1024> words_{};
};

unsigned index_for(std::uint64_t value, unsigned wired) {
  return static_cast<unsigned>(wired > 31 ? value & 63 : value % (32 - wired) + wired);
}
} // namespace

void random_tests() {
  {
    Memory memory;
    Cpu cpu(memory);
    constexpr unsigned sequence[] = {22, 0, 22, 14, 13, 27, 4, 20, 27, 2, 8, 9, 1, 0, 2, 25};
    for (auto value : sequence)
      equal(cpu.read_control(Random), value);
    cpu.power();
    equal(cpu.read_control(Random), 8);
  }

  for (unsigned wired = 0; wired < 64; ++wired) {
    Memory memory;
    Cpu cpu(memory);
    RandomGenerator expected;
    cpu.write_control(Wired, wired);
    for (unsigned n = 0; n < 64; ++n)
      equal(cpu.read_control(Random), index_for(expected(), wired));
  }

  for (unsigned wired = 0; wired < 64; ++wired) {
    RandomMemory memory;
    memory.instruction(0, c(0, 1, Random));
    memory.instruction(1, c(0, 2, Random));
    memory.instruction(2, co(6));
    memory.instruction(3, c(4, 30, Status));
    Cpu cpu(memory);
    RandomGenerator expected;
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Wired, wired);
    cpu.write_control(EntryHi, 0x40008011);
    cpu.write_control(EntryLo0, 0x123456);
    cpu.write_control(EntryLo1, 0x2abcde);
    cpu.state().gpr[30] = 0x30000000;
    cpu.set_pc(0xffffffff80000000);
    for (unsigned n = 0; n < 4 && cpu.state().pc != 0xffffffff80000010; ++n)
      equal(cpu.run_block(cpu.state().clocks), true);
    equal(cpu.state().pc, 0xffffffff80000010);
    equal(cpu.state().gpr[1], index_for(expected(), wired));
    equal(cpu.state().gpr[2], index_for(expected(), wired));
    const auto selected = index_for(expected(), wired);
    for (unsigned index = 0; index < 32; ++index) {
      cpu.write_control(Index, index);
      cpu.execute(co(1));
      equal(cpu.read_control(EntryHi), index == selected ? 0x40008011 : 0);
      equal(cpu.read_control(EntryLo0), index == selected ? 0x123456 : 0);
      equal(cpu.read_control(EntryLo1), index == selected ? 0x2abcde : 0);
    }
    equal(cpu.read_control(Random), index_for(expected(), wired));
  }

  for (bool expansion : {false, true}) {
    ConsoleConfig config;
    config.random_seed = 0;
    config.expansion = expansion;
    Console console(config);
    RandomGenerator expected;
    const auto initialization_reads = expansion ? 8u : 4u;
    for (unsigned n = 0; n < initialization_reads; ++n)
      expected();
    for (unsigned reset = 0; reset < 3; ++reset) {
      console.signal().write_io(16, 1);
      for (unsigned wired : {0u, 7u, 31u, 32u, 63u}) {
        console.cpu().write_control(Wired, wired);
        for (unsigned n = 0; n < 32; ++n) {
          equal(console.cpu().read_control(Random), index_for(expected(), wired));
          equal(console.signal().read_status(0), expected() & 0xfff);
        }
      }
      console.power(true);
    }
    console.power();
    expected.seed(0);
    for (unsigned n = 0; n < initialization_reads; ++n)
      expected();
    equal(console.cpu().read_control(Random), index_for(expected(), 0));
    console.signal().write_io(16, 1);
    equal(console.signal().read_status(0), expected() & 0xfff);
  }
}

} // namespace test
