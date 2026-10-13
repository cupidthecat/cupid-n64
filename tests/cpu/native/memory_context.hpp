#pragma once

#include "../native_memory_fixture.hpp"
#include "core/cpu/execution/instruction.hpp"

namespace test::native_memory {

using ContextFixture = SizedCachedFixture<SizedCachedMemory<32768>>;

inline void context_reuse() {
  for (unsigned base : {28u, 29u}) {
    for (unsigned operation : {35u, 55u, 43u, 63u, 34u, 38u, 42u, 46u}) {
      for (unsigned offset : {0u, 1u, 4u, 0x7fffu, 0x8000u, 0xffffu}) {
        ContextFixture actual, expected;
        for (auto *f : {&actual, &expected}) {
          f->code(0, i(operation, base, 2, static_cast<std::uint16_t>(offset)));
          f->code(4, r(8, 31, 0, 0));
          f->code(8, 0);
        }
        for (auto address : {0xffffffff80010000ull, 0xffffffff80010001ull, 0xffffffff80010004ull,
                             0xffffffff80018000ull, 0xffffffff80018001ull, 0xffffffff80007fffull,
                             0xffffffff80008000ull, 0xffffffffa0010000ull, 0x0000000080010000ull,
                             0xffffffff80000000ull, 0xffffffff80020000ull, 0xffffffff80010000ull}) {
          for (auto *f : {&actual, &expected}) {
            f->cpu.write_control(Status, 0x34000000);
            f->cpu.state().gpr[base] = address;
            f->cpu.state().gpr[2] = 0x8192a3b4c5d6e7f8ull;
            f->cpu.state().gpr[31] = 0xffffffff80003000ull;
            f->cpu.set_pc(0xffffffff80001000ull);
          }
          equal(actual.cpu.run_block(0), expected.cpu.run_interpreted_block(0));
          compare(actual, expected, false);
        }
      }
    }
  }
}

inline void stack_entries() {
  const auto execute = [](ContextFixture &f, bool native) {
    const auto limit = f.cpu.state().clocks + 1024;
    bool ran;
    do {
      ran = native ? f.cpu.run_block(limit) : f.cpu.run_interpreted_block(limit);
    } while (ran && f.cpu.state().pc >= 0xffffffff80001000ull &&
             f.cpu.state().pc < 0xffffffff80001020ull && f.cpu.state().clocks < limit);
    return ran;
  };
  for (unsigned operation : {9u, 25u}) {
    for (unsigned adjustment : {1u, 4u, 8u, 0xfff8u}) {
      for (bool secondary : {false, true}) {
        ContextFixture actual, expected;
        for (auto *f : {&actual, &expected}) {
          f->cpu.state().gpr[29] = 0xffffffff80010000ull;
          f->cpu.execute(i(35, 29, 5, 0));
          f->cpu.state().gpr[31] = 0xffffffff80003000ull;
          f->code(0, i(operation, 29, 29, static_cast<std::uint16_t>(adjustment)));
          f->code(4, i(4, 3, 0, 4));
          f->code(8, 0);
          f->code(12, i(55, 29, 2, 0));
          f->code(16, i(5, 3, 0, 0xfffb));
          f->code(20, i(9, 3, 3, 0xffff));
          f->code(24, r(8, 31, 0, 0));
          f->code(28, 0);
          f->cpu.state().gpr[3] = 2;
        }
        equal(execute(actual, true), execute(expected, false));
        compare(actual, expected, false);
        for (auto *f : {&actual, &expected}) {
          f->cpu.write_control(Status, 0x34000000);
          f->cpu.state().gpr[29] = 0xffffffff80010000ull;
          f->cpu.state().gpr[3] = 1;
          f->cpu.set_pc(secondary ? 0xffffffff8000100cull : 0xffffffff80001000ull);
        }
        equal(execute(actual, true), execute(expected, false));
        compare(actual, expected, false);
      }
    }
  }
}

inline void link_register_bases() {
  for (unsigned base : {28u, 29u}) {
    for (unsigned operation : {35u, 55u}) {
      ContextFixture actual, expected;
      for (auto *f : {&actual, &expected}) {
        f->cpu.state().gpr[base] = 0xffffffff80010000ull;
        f->cpu.state().gpr[1] = 0xffffffff80003000ull;
        f->cpu.state().gpr[4] = 0xffffffff80001008ull;
        f->cpu.execute(i(35, 4, 5, 0));
        f->code(0, 0);
        f->code(4, r(9, 1, 0, base));
        f->code(8, i(operation, base, 2, 0));
        f->code(12, r(8, 1, 0, 0));
        f->code(16, 0);
      }
      equal(actual.cpu.run_block(0), expected.cpu.run_interpreted_block(0));
      compare(actual, expected, false);
      equal(actual.cpu.state().gpr[base], 0xffffffff8000100cull);
      equal(actual.cpu.read_control(Cause) & 0x7c, operation == 55 ? 4u << 2 : 0);
      if (operation == 55) {
        equal(actual.cpu.read_control(Cause) >> 31, 1);
        equal(actual.cpu.read_control(Epc), 0xffffffff80001004ull);
      }
    }
  }
}

inline void memory_context_tests() {
  context_reuse();
  stack_entries();
  link_register_bases();
  for (unsigned base : {28u, 29u}) {
    for (unsigned format : {0u, 1u, 2u}) {
      const auto transfer = (18u << 26) | (format << 21) | (base << 16);
      const auto info = block_instruction(transfer);
      equal(info.terminal, true);
      equal(base == 28 ? info.writes_gp : info.writes_sp, true);
      ContextFixture actual, expected;
      for (auto *f : {&actual, &expected}) {
        f->cpu.write_control(Status, 0x74000000);
        f->cpu.state().gpr[base] = 0xffffffff80010000ull;
        f->cpu.execute(i(35, base, 5, 0));
        f->cpu.state().gpr[3] = 0xffffffff80010001ull;
        f->cpu.execute((18u << 26) | (5u << 21) | (3u << 16));
        f->cpu.state().gpr[31] = 0xffffffff80003000ull;
        f->code(0, transfer);
        f->code(4, i(35, base, 2, 0));
        f->code(8, r(8, 31, 0, 0));
        f->code(12, 0);
        f->cpu.set_pc(0xffffffff80001000ull);
      }
      for (auto *f : {&actual, &expected})
        while (f->cpu.state().pc >= 0xffffffff80001000ull &&
               f->cpu.state().pc < 0xffffffff80001010ull)
          equal(f == &actual ? f->cpu.run_block(0) : f->cpu.run_interpreted_block(0), true);
      compare(actual, expected, false);
      equal(actual.cpu.read_control(Cause) & 0x7c, 4u << 2);
      equal(actual.cpu.read_control(Epc), 0xffffffff80001004ull);
    }
    for (unsigned function : {0x20u, 0x29u}) {
      const auto instruction = 0x42000000u | (base << (function == 0x20 ? 20 : 15)) | function;
      const auto info = block_instruction(instruction);
      equal(base == 28 ? info.writes_gp : info.writes_sp, true);
    }
  }
}

} // namespace test::native_memory
