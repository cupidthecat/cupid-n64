#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct LoopMemory : Memory {
  enum Action { None, Freeze, Nmi, Irq } action = None;
  std::array<std::uint32_t, 2048> words{};
  Cpu *cpu = nullptr;
  bool stopped = false;
  std::vector<std::pair<std::uint32_t, unsigned>> bursts;
  bool frozen() const override {
    return stopped;
  }
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    bursts.emplace_back(address, static_cast<unsigned>(output.size() * 4));
    if (success)
      for (unsigned n = 0; n < output.size(); ++n)
        output[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    const auto result = Memory::write(address, bytes, value);
    if (action == Freeze)
      stopped = true;
    if (action == Nmi)
      cpu->request_nmi();
    if (action == Irq)
      cpu->set_interrupt(2, true);
    return result;
  }
};

struct LoopFixture {
  LoopMemory memory;
  Cpu cpu{memory};
  std::uint64_t start = 0xffffffff80001000;
  LoopFixture() {
    memory.cpu = &cpu;
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(start);
  }
  void code(unsigned offset, std::uint32_t instruction) {
    memory.words[((static_cast<unsigned>(start) & 0x1fff) + offset) / 4] = instruction;
  }
};

void compare(LoopFixture &actual, LoopFixture &expected) {
  const auto &a = actual.cpu.state();
  const auto &b = expected.cpu.state();
  equal(a.pc, b.pc);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  equal(actual.memory.frozen(), expected.memory.frozen());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(a.gpr[reg], b.gpr[reg]);
    equal(a.fpr[reg], b.fpr[reg]);
    if (reg != Count)
      equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
  equal(a.fcr31, b.fcr31);
}

void section_loop_tests() {
  for (unsigned bytes : {64u, 96u, 256u, 4096u}) {
    for (bool warm : {false, true}) {
      for (unsigned budget : {0u, 1u, 64u, 50000u}) {
        LoopFixture actual, expected;
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.state().gpr[2] = 3;
          fixture->cpu.state().gpr[28] = 0xffffffff80000200ull;
          fixture->cpu.state().gpr[29] = 0xffffffff80000400ull;
          fixture->cpu.state().gpr[31] = 0xffffffff80003000ull;
          fixture->code(0, i(9, 1, 1, 1));
          fixture->code(4, i(9, 2, 2, 0xffff));
          fixture->code(bytes - 16, i(5, 2, 0, static_cast<std::uint16_t>(-(int(bytes - 12) / 4))));
          fixture->code(bytes - 12, i(9, 3, 3, 1));
          fixture->code(bytes - 8, r(8, 31, 0, 0));
          fixture->code(bytes - 4, 0);
          if (warm) {
            fixture->cpu.state().gpr[4] = fixture->start;
            for (unsigned line = 0; line < bytes; line += 32)
              fixture->cpu.execute(i(47, 4, 20, static_cast<std::uint16_t>(line)));
          }
          fixture->cpu.set_pc(fixture->start);
        }
        const auto limit = actual.cpu.state().clocks + budget;
        equal(actual.cpu.run_block(limit), true);
        do {
          equal(expected.cpu.run_interpreted_block(limit), true);
        } while (expected.cpu.state().pc >= expected.start &&
                 expected.cpu.state().pc < expected.start + bytes &&
                 expected.cpu.state().clocks < limit);
        compare(actual, expected);
        equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
        equal(actual.cpu.read_control(Count), expected.cpu.read_control(Count));
        equal(actual.memory.bursts == expected.memory.bursts, true);
        if (budget == 50000) {
          equal(actual.cpu.state().pc, 0xffffffff80003000ull);
          equal(actual.cpu.state().gpr[1], 3);
          equal(actual.cpu.state().gpr[2], 0);
          equal(actual.cpu.state().gpr[3], 3);
        }
        for (unsigned line = 0; line < bytes; line += 32) {
          for (auto *fixture : {&actual, &expected}) {
            fixture->cpu.state().gpr[4] = fixture->start + line;
            fixture->cpu.execute(i(47, 4, 4, 0));
          }
          equal(actual.cpu.read_control(TagLo), expected.cpu.read_control(TagLo));
        }
      }
    }
  }
  for (unsigned condition : {0u, 1u}) {
    for (unsigned budget : {0u, 50000u}) {
      LoopFixture actual, expected;
      for (auto *fixture : {&actual, &expected}) {
        fixture->cpu.state().gpr[1] = condition;
        fixture->cpu.state().gpr[31] = 0xffffffff80003000ull;
        fixture->code(0, i(4, 1, 0, 15));
        fixture->code(4, i(9, 3, 3, 1));
        fixture->code(64, i(9, 8, 8, 1));
        fixture->code(68, i(9, 9, 9, 1));
        fixture->code(72, r(8, 31, 0, 0));
        fixture->code(76, 0);
      }
      const auto limit = actual.cpu.state().clocks + budget;
      equal(actual.cpu.run_block(limit), true);
      do {
        equal(expected.cpu.run_interpreted_block(limit), true);
      } while (expected.cpu.state().pc >= expected.start &&
               expected.cpu.state().pc < expected.start + 80 &&
               expected.cpu.state().clocks < limit);
      compare(actual, expected);
      equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
      equal(actual.cpu.read_control(Count), expected.cpu.read_control(Count));
      equal(actual.memory.bursts == expected.memory.bursts, true);
      if (budget) {
        equal(actual.cpu.state().pc, 0xffffffff80003000ull);
        equal(actual.cpu.state().gpr[8], 1);
        equal(actual.cpu.state().gpr[9], 1);
      }
    }
  }
  {
    LoopFixture actual, expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->cpu.state().gpr[2] = 3;
      fixture->cpu.state().gpr[4] = 0xffffffff80000200ull;
      fixture->cpu.state().gpr[31] = 0xffffffff80003000ull;
      fixture->cpu.execute(i(35, 4, 6, 0));
      fixture->code(0, i(9, 1, 1, 1));
      fixture->code(4, i(9, 2, 2, 0xffff));
      fixture->code(8, i(35, 4, 6, 0));
      fixture->code(12, i(43, 4, 1, 0));
      fixture->code(80, i(5, 2, 0, 0xffeb));
      fixture->code(84, i(9, 3, 3, 1));
      fixture->code(88, r(8, 31, 0, 0));
      fixture->code(92, 0);
      fixture->cpu.set_pc(fixture->start);
    }
    equal(actual.cpu.run_block(50000), true);
    do {
      equal(expected.cpu.run_interpreted_block(50000), true);
    } while (expected.cpu.state().pc >= expected.start &&
             expected.cpu.state().pc < expected.start + 96);
    compare(actual, expected);
    equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
    equal(actual.cpu.read_control(Count), expected.cpu.read_control(Count));
    equal(actual.memory.bursts == expected.memory.bursts, true);
    equal(actual.cpu.state().gpr[6], 2);
    for (auto *fixture : {&actual, &expected}) {
      fixture->cpu.execute(i(35, 4, 6, 0));
      fixture->cpu.execute(i(47, 4, 21, 0));
    }
    compare(actual, expected);
    equal(actual.cpu.state().clocks, expected.cpu.state().clocks);
    equal(actual.cpu.state().gpr[6], 3);
    equal(actual.memory.get(0x200, 4), 3);
    equal(actual.memory.get(0x200, 4), expected.memory.get(0x200, 4));
  }
  for (auto action : {LoopMemory::Freeze, LoopMemory::Nmi, LoopMemory::Irq}) {
    LoopFixture actual, expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->memory.action = action;
      fixture->cpu.write_control(Status, 0x30000401);
      fixture->cpu.state().gpr[1] = 1;
      fixture->cpu.state().gpr[4] = 0xffffffffa0000200ull;
      fixture->code(0, i(9, 3, 3, 1));
      fixture->code(4, i(43, 4, 3, 0));
      fixture->code(64, i(5, 1, 0, 0xffef));
      fixture->code(68, i(9, 5, 5, 1));
      fixture->code(72, r(8, 31, 0, 0));
      fixture->code(76, 0);
    }
    equal(actual.cpu.run_block(50000), expected.cpu.run_interpreted_block(50000));
    compare(actual, expected);
    equal(actual.memory.bursts == expected.memory.bursts, true);
    equal(actual.cpu.state().gpr[3], 1);
    equal(actual.cpu.state().gpr[5], 1);
  }
}

} // namespace

void native_loop_tests() {
  section_loop_tests();
  for (bool little : {false, true}) {
    for (unsigned program = 0; program < 6; ++program) {
      for (std::uint64_t budget : {0u, 1u, 98u, 104u, 105u, 200u, 257u, 4096u}) {
        LoopFixture actual;
        LoopFixture expected;
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
          fixture->cpu.state().gpr[4] = 0xffffffff80000200;
          fixture->code(0, i(9, 0, 1, 0));
          fixture->code(4, i(9, 1, 1, 1));
          fixture->code(8, program == 1   ? i(5, 1, 0, 0xfffe)
                           : program == 2 ? i(20, 0, 0, 0xfffe)
                           : program == 3 ? 0x0c000401
                                          : 0x08000401);
          fixture->code(12, i(9, 3, 3, 1));
          fixture->code(16, 0x08000404);
          fixture->code(20, 0);
          if (program == 4) {
            fixture->code(0, i(4, 0, 0, 3));
            fixture->code(4, i(9, 3, 3, 1));
            fixture->code(8, 0);
            fixture->code(12, 0);
            fixture->code(16, i(9, 1, 1, 1));
            fixture->code(20, 0x08000400);
            fixture->code(24, i(9, 3, 3, 1));
          }
          if (program == 5) {
            fixture->code(0, i(35, 4, 2, 0));
            fixture->code(4, i(9, 2, 2, 1));
            fixture->code(8, i(43, 4, 2, 0));
            fixture->code(12, 0x08000400);
            fixture->code(16, i(9, 3, 3, 1));
          }
        }
        equal(actual.cpu.run_block(budget), true);
        do {
          equal(expected.cpu.run_interpreted_block(budget), true);
        } while (expected.cpu.state().gpr[3] < actual.cpu.state().gpr[3]);
        compare(actual, expected);
      }
    }
  }
  for (bool little : {false, true}) {
    for (unsigned family = 0; family < 20; ++family) {
      for (unsigned slot = 0; slot < 7; ++slot) {
        LoopFixture actual;
        LoopFixture expected;
        for (auto *fixture : {&actual, &expected}) {
          fixture->cpu.write_control(Status, 0x34000000);
          fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
          fixture->cpu.state().gpr[4] =
              slot >= 3 && slot <= 4 ? 0xffffffffa0000200 : 0xffffffff80000200;
          fixture->cpu.state().gpr[1] = (family % 4 == 1 || family % 4 == 3) ? 1 : 0;
          fixture->cpu.state().fpr[4] = 0x3f800000;
          fixture->cpu.state().fpr[5] = 0x40000000;
          std::uint32_t branch;
          if (family < 8) {
            const auto opcode = family < 4 ? 4 + family : 20 + family - 4;
            branch = i(opcode, 1, family % 4 < 2 ? 1 - family % 4 : 0, 0xfffe);
          } else if (family < 16) {
            const auto function = family < 12 ? family - 8 : family + 4;
            fixture->cpu.state().gpr[1] = (function & 1) ? 0 : ~0ull;
            branch = i(1, 1, function, 0xfffe);
          } else {
            fixture->cpu.state().fcr31 = ((family - 16) & 1) << 23;
            branch = i(17, 8, family - 16, 0xfffe);
          }
          constexpr std::array<std::uint32_t, 7> slots{
              i(9, 2, 2, 1),  i(35, 4, 2, 0), i(43, 4, 2, 0), i(35, 4, 2, 0),
              i(43, 4, 2, 0), 0x46052180,     i(55, 4, 2, 0)};
          fixture->code(0, i(9, 3, 3, 1));
          fixture->code(4, branch);
          fixture->code(8, slots[slot]);
        }
        const std::uint64_t budget = 257;
        equal(actual.cpu.run_block(budget), true);
        do {
          equal(expected.cpu.run_interpreted_block(budget), true);
        } while (expected.cpu.state().gpr[3] < actual.cpu.state().gpr[3]);
        compare(actual, expected);
      }
    }
  }
  for (unsigned condition : {7u, 23u}) {
    LoopFixture actual;
    LoopFixture expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->cpu.state().gpr[1] = 16;
      fixture->code(0, i(9, 1, 1, 0xffff));
      fixture->code(4, i(condition, 1, 0, 0xfffe));
      fixture->code(8, i(9, 3, 3, 1));
      fixture->code(12, i(9, 0, 28, 7));
    }
    const std::uint64_t budget = 4096;
    equal(actual.cpu.run_block(budget), true);
    do {
      equal(expected.cpu.run_interpreted_block(budget), true);
    } while (expected.cpu.state().pc == expected.start);
    compare(actual, expected);
  }
  for (auto action : {LoopMemory::Freeze, LoopMemory::Nmi, LoopMemory::Irq}) {
    LoopFixture actual;
    LoopFixture expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->memory.action = action;
      fixture->cpu.write_control(Status, 0x30000401);
      fixture->cpu.state().gpr[4] = 0xffffffffa0000200;
      fixture->code(0, i(43, 4, 2, 0));
      fixture->code(4, 0x08000400);
      fixture->code(8, i(9, 3, 3, 1));
    }
    const std::uint64_t budget = 4096;
    equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
    compare(actual, expected);
    equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
    compare(actual, expected);
  }
  for (auto start : {0xffffffff80001000ull, 0xffffffff80000180ull}) {
    LoopFixture actual;
    LoopFixture expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->start = start;
      fixture->cpu.set_pc(start);
      fixture->cpu.state().gpr[4] = 0xffffffffa0000200;
      fixture->code(0, (2u << 26) | (static_cast<unsigned>(start >> 2) & 0x3ffffff));
      fixture->code(4, i(35, 4, 2, 0));
      fixture->cpu.step();
      fixture->cpu.set_pc(start);
      fixture->memory.success = false;
    }
    const std::uint64_t budget = 4096;
    equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
    compare(actual, expected);
    equal(actual.cpu.read_control(Epc), start);
    equal(actual.cpu.read_control(Cause) & 0x80000000, 0x80000000);
  }
  for (unsigned compare_tick : {1u, 64u, 128u}) {
    LoopFixture actual;
    LoopFixture expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->cpu.write_control(Status, 0x30008001);
      fixture->cpu.state().gpr[1] = 1;
      fixture->code(0, i(9, 3, 3, 1));
      fixture->code(4, i(5, 1, 0, 0xfffe));
      fixture->code(8, 0);
      fixture->cpu.step();
      fixture->cpu.set_pc(fixture->start);
      fixture->cpu.write_control(Count, 0);
      fixture->cpu.write_control(Compare, compare_tick);
    }
    do {
      const auto target = actual.cpu.state().clocks + actual.cpu.synchronization_limit();
      equal(actual.cpu.run_block(target), true);
      do {
        equal(expected.cpu.run_interpreted_block(target), true);
      } while (expected.cpu.state().clocks < target);
      actual.cpu.synchronize_timer();
      expected.cpu.synchronize_timer();
      compare(actual, expected);
    } while (!(actual.cpu.read_control(Cause) & 0x8000));
    const std::uint64_t budget = 4096;
    equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
    compare(actual, expected);
  }
  for (unsigned layout : {0u, 1u, 2u, 3u}) {
    LoopFixture actual;
    LoopFixture expected;
    for (auto *fixture : {&actual, &expected}) {
      fixture->cpu.state().gpr[5] = fixture->start;
      fixture->code(0, layout == 1 ? i(47, 5, 16, 0) : 0);
      fixture->code(4, layout == 0 ? 0x08000402 : 0x08000400);
      fixture->code(8, i(9, 3, 3, 1));
      if (layout == 2) {
        fixture->code(4, i(20, 1, 0, 0xfffe));
        fixture->cpu.state().gpr[1] = 1;
      }
      if (layout == 3) {
        fixture->code(4, 0);
        for (unsigned offset = 8; offset < 28; offset += 4)
          fixture->code(offset, 0);
        fixture->code(28, 0x08000400);
        fixture->code(32, i(9, 3, 3, 1));
      }
    }
    const std::uint64_t budget = 4096;
    equal(actual.cpu.run_block(budget), expected.cpu.run_interpreted_block(budget));
    compare(actual, expected);
  }
}

} // namespace test
