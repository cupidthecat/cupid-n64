#include "../../devices/fixture.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct FrozenMemory : MemoryFixture {
  bool stopped = false;
  unsigned bursts = 0;
  unsigned writes = 0;
  FrozenMemory() {
    initialize();
    mi.connect([this](bool line) { cpu.set_interrupt(2, line); }, [this] { stopped = true; });
  }
  bool frozen() const override {
    return stopped;
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    ++bursts;
    if (address <= 0x03ffffff)
      return MemoryFixture::read_burst(address, output);
    stopped = true;
    return {0, false};
  }
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> input) override {
    ++writes;
    if (address <= 0x03ffffff)
      return MemoryFixture::write_burst(address, input);
    stopped = true;
    return {0, false};
  }
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return ram.identity() && address < ram.size() ? ram.words().subspan(address / 4)
                                                  : std::span<const std::uint32_t>();
  }
  std::span<const std::uint32_t> cache_fill_data(std::uint32_t address) const override {
    return instruction_data(address);
  }
  bool instruction_coherent(std::uint32_t address, std::span<const std::uint32_t> words) override {
    std::array<std::uint32_t, 8> data{};
    read_burst(address, data);
    return !stopped && std::equal(words.begin(), words.end(), data.begin());
  }
  InstructionTracker *instruction_tracker() override {
    return ram.instruction_tracker();
  }
  void prepare(std::uint32_t instruction) {
    ram.write(0x1000, 4, instruction);
    ram.write(0x1004, 4, c(4, 5, Status));
    cpu.state().gpr[2] = 0xffffffff80001000ull;
    cpu.state().gpr[5] = 0x30000000;
    cpu.execute(i(0x2f, 2, 20, 0));
    cpu.set_pc(0xffffffff80001000ull);
    cpu.write_control(Count, 0);
    cpu.write_control(Compare, 0xffffffff);
  }
  std::uint64_t run(bool native) {
    const auto start = cpu.state().clocks;
    if (native)
      equal(cpu.run_block(start + 4096), true);
    else
      cpu.step();
    return cpu.state().clocks - start;
  }
  void retired(unsigned clocks, unsigned instructions, bool frozen = true) {
    equal(cpu.read_control(Count), clocks / 4);
    equal(cpu.state().pc, 0xffffffff80001000ull + instructions * 4);
    equal(cpu.read_control(Cause), 0);
    equal(cpu.read_control(Epc), 0);
    equal(cpu.in_delay_slot(), false);
    equal(stopped, frozen);
    const auto next = cpu.state().clocks;
    cpu.step();
    equal(cpu.state().clocks - next, 2);
    equal(cpu.state().pc, 0xffffffff80001000ull + (instructions + !frozen) * 4);
    stopped = false;
  }
  std::uint64_t tag(std::uint64_t address, bool instruction) {
    cpu.state().gpr[3] = address;
    cpu.execute(i(0x2f, 3, instruction ? 4 : 5, 0));
    return cpu.read_control(TagLo);
  }
  void data(std::uint64_t address, const std::array<std::uint32_t, 4> &words, bool dirty) {
    const auto saved = tag(address, false);
    cpu.write_control(TagLo, saved | 0x80);
    cpu.execute(i(0x2f, 3, 9, 0));
    for (unsigned n = 0; n < words.size(); ++n) {
      cpu.execute(i(35, 3, 4, n * 4));
      equal(static_cast<std::uint32_t>(cpu.state().gpr[4]), words[n]);
    }
    const auto before = writes;
    cpu.execute(i(0x2f, 3, 25, 0));
    equal(writes - before, dirty ? 1 : 0);
  }
};

void instruction_fill(unsigned physical, unsigned mode, bool warm) {
  FrozenMemory f;
  f.ram.write(0x2000, 4, i(9, 0, 4, 11));
  f.ram.write(0x6000, 4, i(9, 0, 4, 17));
  f.ram.write(0x6004, 4, c(4, 5, Status));
  f.cpu.state().gpr[3] = 0xffffffff80002000ull;
  f.cpu.state().gpr[5] = 0x30000000;
  if (warm)
    f.cpu.execute(i(0x2f, 3, 20, 0));
  const bool mapped = physical == 0x6000;
  const bool direct = mapped && mode == 2;
  const auto pc = 0xffffffff80000000ull | physical;
  f.cpu.set_pc(pc);
  f.cpu.write_control(Count, 0);
  f.cpu.write_control(Compare, 0xffffffff);
  if (mapped)
    f.mi.write_word(0, 0x400);
  f.bursts = 0;
  const auto start = f.cpu.state().clocks;
  const bool handled = mode == 2 ? f.cpu.run_block(start + 4096)
                                 : mode == 1 && f.cpu.run_interpreted_block(start + 4096);
  equal(handled, mapped && mode != 0);
  if (!handled)
    f.cpu.step();
  equal(f.cpu.state().clocks - start, direct ? 100 : 98);
  equal(f.cpu.read_control(Count), direct ? 25 : 24);
  equal(f.cpu.state().pc, pc + (direct ? 8 : 4));
  equal(f.cpu.state().gpr[4], direct ? 17 : warm ? 11 : 0);
  equal(f.cpu.read_control(Cause), 0);
  equal(f.cpu.read_control(Epc), 0);
  equal(f.cpu.in_delay_slot(), false);
  equal(f.stopped, !direct);
  equal(f.mi.frozen(), mapped && !direct);
  equal(f.bursts, direct ? 0 : 1);
  const auto next = f.cpu.state().clocks;
  f.cpu.step();
  equal(f.cpu.state().clocks - next, 2);
  equal(f.cpu.state().pc, pc + (direct ? 12 : 4));

  // Release the fixture's freeze to inspect the retained cache line.
  f.stopped = false;
  f.cpu.state().gpr[3] = pc;
  f.cpu.execute(i(0x2f, 3, 4, 0));
  equal(f.cpu.read_control(TagLo), ((physical & ~0xfffu) >> 4) | 0x80);
  f.cpu.state().gpr[4] = 0;
  f.cpu.set_pc(pc);
  f.cpu.step();
  equal(f.cpu.state().gpr[4], direct ? 17 : warm ? 11 : 0);
}

void data_fill(bool native, bool ebus, unsigned warm, bool store) {
  FrozenMemory f;
  std::array<std::uint32_t, 4> words{};
  for (unsigned n = 0; n < words.size(); ++n) {
    f.ram.write(0x2000 + n * 4, 4, 0x11223300 + n);
    f.ram.write(0x6000 + n * 4, 4, 0x55667700 + n);
    if (warm)
      words[n] = 0x11223300 + n;
  }
  f.cpu.state().gpr[3] = 0xffffffff80002000ull;
  f.cpu.state().gpr[4] = 0x19283746;
  if (warm)
    f.cpu.execute(i(warm == 1 ? 35 : 43, 3, 4, 0));
  if (warm == 2)
    words[0] = 0x19283746;
  const auto address = ebus ? 0xffffffff80006000ull : 0xffffffff84302000ull;
  f.cpu.state().gpr[3] = address;
  f.cpu.state().gpr[4] = 0x12345678;
  f.prepare(i(store ? 43 : 35, 3, 4, 0));
  if (ebus)
    f.mi.write_word(0, 0x400);
  const auto clocks = 82u + (warm == 2 ? 80 : 0) + (native && !ebus ? 4 : 0);
  equal(f.run(native), clocks);
  equal(f.cpu.state().gpr[4], store ? 0x12345678 : words[0]);
  f.retired(clocks, native && !ebus ? 2 : 1);
  equal(f.mi.frozen(), ebus);
  equal(f.tag(address, false), ebus ? 0x6c0 : 0x430200);
  if (store)
    words[0] = 0x12345678;
  f.data(address, words, store);
}

void failed_writeback(bool native, unsigned operation) {
  FrozenMemory f;
  std::array<std::uint32_t, 4> words{};
  for (unsigned n = 0; n < words.size(); ++n) {
    words[n] = 0x11223300 + n;
    f.ram.write(0x2000 + n * 4, 4, words[n]);
    f.ram.write(0x6000 + n * 4, 4, 0x55667700 + n);
  }
  f.cpu.state().gpr[3] = 0xffffffff80002000ull;
  f.cpu.state().gpr[4] = 0x19283746;
  f.cpu.execute(i(43, 3, 4, 0));
  words[0] = 0x19283746;
  f.cpu.write_control(TagLo, 0x4302c0);
  f.cpu.execute(i(0x2f, 3, 9, 0));
  const bool replace = operation == 1 || operation == 13 || operation == 35 || operation == 43;
  const bool transfer = operation == 35 || operation == 43;
  const auto address = replace ? 0xffffffff80006000ull : 0xffffffff84302000ull;
  f.cpu.state().gpr[3] = address;
  f.cpu.state().gpr[4] = 0x12345678;
  f.prepare(transfer ? i(operation, 3, 4, 0) : i(0x2f, 3, operation, 0));
  const auto clocks =
      (operation == 17 ? 2u : 82u) + (transfer ? 80 : 0) + (native ? transfer ? 4 : 2 : 0);
  equal(f.run(native), clocks);
  equal(f.cpu.state().gpr[4], operation == 35 ? 0x55667700 : 0x12345678);
  f.retired(clocks, native ? 2 : 1, operation != 17);
  const auto expected_tag = operation == 1    ? 0x600u
                            : replace         ? 0x6c0u
                            : operation == 25 ? 0x4302c0u
                                              : 0x430200u;
  equal(f.tag(address, false), expected_tag);
  if (transfer)
    for (unsigned n = 0; n < words.size(); ++n)
      words[n] = 0x55667700 + n;
  if (operation == 43)
    words[0] = 0x12345678;
  const bool dirty = operation == 1 || operation == 17 || operation == 21 || operation == 43;
  f.data(address, words, dirty);
}

void instruction_operation(bool native, bool ebus, unsigned operation) {
  FrozenMemory f;
  f.ram.write(0x2000, 4, i(9, 0, 4, 11));
  f.cpu.state().gpr[3] = 0xffffffff80002000ull;
  f.cpu.execute(i(0x2f, 3, 20, 0));
  const auto address = ebus ? 0xffffffff80006000ull : 0xffffffff84302000ull;
  const auto physical = static_cast<std::uint32_t>(address) & 0x1fffffff;
  if (operation == 24) {
    f.cpu.write_control(TagLo, (physical >> 4) | 0x80);
    f.cpu.execute(i(0x2f, 3, 8, 0));
  }
  f.cpu.state().gpr[3] = address;
  f.prepare(i(0x2f, 3, operation, 0));
  if (ebus)
    f.mi.write_word(0, 0x400);
  const auto clocks = native && !ebus ? 100u : 98u;
  equal(f.run(native), clocks);
  f.retired(clocks, native && !ebus ? 2 : 1);
  equal(f.tag(address, true), (physical >> 4) | 0x80);
  f.cpu.state().gpr[4] = 0;
  f.cpu.set_pc(address);
  f.cpu.step();
  equal(f.cpu.state().gpr[4], 11);
}

} // namespace

void frozen_cache_tests() {
  for (unsigned mode : {0u, 1u, 2u}) {
    if (mode == 2 && sizeof(void *) != 8)
      continue;
    for (bool warm : {false, true}) {
      instruction_fill(0x6000, mode, warm);
      for (unsigned physical : {0x04302000u, 0x04402000u, 0x1fc02000u})
        instruction_fill(physical, mode, warm);
    }
  }
  {
    FrozenMemory f;
    f.cpu.state().gpr[3] = 0xffffffff80006000ull;
    f.cpu.state().gpr[4] = 0x12345678;
    f.prepare(i(35, 3, 4, 0));
    f.mi.write_word(0, 0x400);
    const auto start = f.cpu.state().clocks;
    equal(f.cpu.run_interpreted_block(start + 4096), true);
    equal(f.cpu.state().clocks - start, 82);
    equal(f.cpu.state().gpr[4], 0);
    f.retired(82, 1);
  }
  for (bool native : {false, true}) {
    if (native && sizeof(void *) != 8)
      continue;
    for (bool ebus : {false, true}) {
      for (unsigned warm : {0u, 1u, 2u})
        for (bool store : {false, true})
          data_fill(native, ebus, warm, store);
      for (unsigned operation : {20u, 24u})
        instruction_operation(native, ebus, operation);
    }
    for (unsigned operation : {1u, 13u, 17u, 21u, 25u, 35u, 43u})
      failed_writeback(native, operation);
  }
}

} // namespace test
