#include "../native_memory_fixture.hpp"
#include "core/memory/instruction_tracker.hpp"

namespace test {
using namespace cupid::n64;
namespace {

void capture_order() {
  for (bool interpreted : {false, true})
    for (bool little : {false, true})
      for (unsigned privilege = 0; privilege < 4; ++privilege)
        for (bool extended : {false, true})
          for (unsigned level : {0u, 2u, 4u})
            for (bool reverse : {false, true}) {
              native_memory::CachedFixture f;
              f.cpu.state().gpr.fill(0);
              f.code(0, i(9, 0, 6, 17));
              f.code(4, i(9, 6, 7, 29));
              f.code(8, r(8, 31, 0, 0));
              f.code(12, i(9, 8, 8, 1));
              f.code(16, 0);
              f.code(20, 0);
              for (unsigned stage = 0; stage < 4; ++stage) {
                const bool re = reverse ^ (stage == 3);
                const auto status = 0x34000000u | (privilege << 3) | (extended ? 0xe0u : 0) |
                                    level | (re ? 0x02000000u : 0);
                f.cpu.write_control(Config, (little ^ (stage >= 2)) ? 0x70066460 : 0x7006e460);
                f.cpu.write_control(Status, status);
                for (unsigned reg : {6u, 7u, 8u})
                  f.cpu.state().gpr[reg] = 0;
                f.cpu.state().gpr[31] = 0xffffffff80003000ull;
                f.cpu.set_pc(0xffffffff80001000ull);
                f.cpu.write_control(Count, 0);
                f.cpu.write_control(Compare, 0xffffffff);
                const auto start = f.cpu.state().clocks;
                equal(interpreted ? f.cpu.run_interpreted_block(start + 10000)
                                  : f.cpu.run_block(start + 10000),
                      true);
                const bool native_reverse = privilege >= 2 && !level && re;
                const unsigned clocks = (stage ? 0 : 96) + (native_reverse ? 10 : 8);
                equal(f.cpu.state().clocks - start, clocks);
                equal(f.cpu.read_control(Count), clocks / 4);
                equal(f.cpu.state().pc, 0xffffffff80003000ull);
                equal(f.cpu.in_delay_slot(), false);
                equal(f.cpu.read_control(Cause), 0);
                equal(f.cpu.read_control(Epc), 0);
                equal(f.cpu.read_control(Status), status);
                for (unsigned reg = 0; reg < 32; ++reg)
                  equal(f.cpu.state().gpr[reg], reg == 6    ? 17
                                                : reg == 7  ? native_reverse ? 29 : 46
                                                : reg == 8  ? 1
                                                : reg == 31 ? 0xffffffff80003000ull
                                                            : 0);
              }
            }
}

void hit_order() {
  struct Case {
    unsigned opcode;
    bool linked;
    std::uint64_t result, data, floating;
  };
  constexpr std::uint64_t initial = 0x89abcdef01234567ull;
  constexpr std::array<Case, 29> cases = {{
      {26, false, 0x1020304050607080ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {27, false, 0x89abcdef01234510ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {32, false, 0x10ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {33, false, 0x1020ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {34, false, 0x10203040ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {35, false, 0x10203040ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {36, false, 0x10ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {37, false, 0x1020ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {38, false, 0x89abcdef01234510ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {39, false, 0x10203040ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {40, false, 0x89abcdef01234567ull, 0x6720304050607080ull, 0x89abcdef01234567ull},
      {41, false, 0x89abcdef01234567ull, 0x4567304050607080ull, 0x89abcdef01234567ull},
      {42, false, 0x89abcdef01234567ull, 0x123456750607080ull, 0x89abcdef01234567ull},
      {43, false, 0x89abcdef01234567ull, 0x123456750607080ull, 0x89abcdef01234567ull},
      {44, false, 0x89abcdef01234567ull, 0x89abcdef01234567ull, 0x89abcdef01234567ull},
      {45, false, 0x89abcdef01234567ull, 0x6720304050607080ull, 0x89abcdef01234567ull},
      {46, false, 0x89abcdef01234567ull, 0x6720304050607080ull, 0x89abcdef01234567ull},
      {48, false, 0x10203040ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {49, false, 0x89abcdef01234567ull, 0x1020304050607080ull, 0x89abcdef10203040ull},
      {52, false, 0x1020304050607080ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {53, false, 0x89abcdef01234567ull, 0x1020304050607080ull, 0x1020304050607080ull},
      {55, false, 0x1020304050607080ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {56, false, 0x0ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {56, true, 0x1ull, 0x123456750607080ull, 0x89abcdef01234567ull},
      {57, false, 0x89abcdef01234567ull, 0x123456750607080ull, 0x89abcdef01234567ull},
      {60, false, 0x0ull, 0x1020304050607080ull, 0x89abcdef01234567ull},
      {60, true, 0x1ull, 0x89abcdef01234567ull, 0x89abcdef01234567ull},
      {61, false, 0x89abcdef01234567ull, 0x89abcdef01234567ull, 0x89abcdef01234567ull},
      {63, false, 0x89abcdef01234567ull, 0x89abcdef01234567ull, 0x89abcdef01234567ull},
  }};
  for (bool interpreted : {false, true})
    for (bool little : {false, true})
      for (const auto &test : cases) {
        native_memory::CachedFixture f;
        f.cpu.write_control(Status, 0x34000000);
        f.cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
        f.memory.words[0x200 / 4] = 0x10203040;
        f.memory.words[0x204 / 4] = 0x50607080;
        f.memory.words[0x208 / 4] = 0x90a0b0c0;
        f.memory.words[0x20c / 4] = 0;
        f.cpu.state().gpr[2] = 0xffffffff80000200ull;
        f.cpu.execute(i(test.linked ? 48 : 35, 2, 9, 0));
        f.cpu.state().gpr[8] = initial;
        f.cpu.state().fpr[8] = initial;
        f.cpu.state().gpr[31] = 0xffffffff80003000ull;
        f.code(0, i(test.opcode, 2, 8, 0));
        f.code(4, r(8, 31, 0, 0));
        f.code(8, 0);
        f.cpu.set_pc(0xffffffff80001000ull);
        f.cpu.write_control(Count, 0);
        f.cpu.write_control(Compare, 0xffffffff);
        const auto start = f.cpu.state().clocks;
        equal(interpreted ? f.cpu.run_interpreted_block(start + 10000)
                          : f.cpu.run_block(start + 10000),
              true);
        equal(f.cpu.state().gpr[8], test.result);
        equal(f.cpu.state().fpr[8], test.floating);
        equal(f.cpu.state().pc, 0xffffffff80003000ull);
        equal(f.cpu.read_control(Cause), 0);
        equal(f.cpu.read_control(Epc), 0);
        equal(f.cpu.read_control(LlAddr),
              test.linked || test.opcode == 48 || test.opcode == 52 ? 0x20 : 0);
        if (!interpreted)
          equal(f.cpu.state().clocks - start, 104);
        f.cpu.execute(i(55, 2, 10, 0));
        equal(f.cpu.state().gpr[10], test.data);
        f.cpu.execute(i(55, 2, 10, 8));
        equal(f.cpu.state().gpr[10], 0x90a0b0c000000000ull);
      }
}

void ram_boundary_order() {
  for (bool interpreted : {false, true})
    for (bool little : {false, true})
      for (bool outside : {false, true}) {
        native_memory::CachedFixture f;
        f.cpu.write_control(Status, 0x34000000);
        f.cpu.state().gpr[2] = 0xffffffff80000000ull + sizeof(f.memory.words) - (outside ? 0 : 16);
        f.cpu.state().gpr[9] = 0x11223344;
        f.cpu.execute(i(43, 2, 9, 0));
        f.cpu.state().gpr[9] = 0x55667788;
        f.cpu.execute(i(43, 2, 9, 4));
        f.cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
        f.cpu.state().gpr[31] = 0xffffffff80003000ull;
        f.code(0, i(35, 2, 8, 0));
        f.code(4, r(8, 31, 0, 0));
        f.code(8, 0);
        f.cpu.set_pc(0xffffffff80001000ull);
        const auto start = f.cpu.state().clocks;
        equal(interpreted ? f.cpu.run_interpreted_block(start + 10000)
                          : f.cpu.run_block(start + 10000),
              true);
        equal(f.cpu.state().gpr[8], outside && little ? 0x55667788 : 0x11223344);
        equal(f.cpu.state().pc, 0xffffffff80003000ull);
        equal(f.cpu.read_control(Cause), 0);
        if (!interpreted)
          equal(f.cpu.state().clocks - start, outside ? 106 : 104);
      }
}

struct CoherentMemory : native_memory::CachedMemory {
  InstructionTracker tracker{sizeof(words)};
  InstructionTracker *instruction_tracker() override {
    return &tracker;
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    equal(bytes, 4);
    words[address / 4] = static_cast<std::uint32_t>(value);
    tracker.invalidate(address, bytes);
    return {};
  }
};

void stale_capture_order() {
  for (bool interpreted : {false, true})
    for (bool little : {false, true}) {
      CoherentMemory memory;
      Cpu cpu(memory);
      const std::array code{i(9, 0, 6, 17), i(9, 6, 7, 29), r(8, 31, 0, 0), i(9, 8, 8, 1), 0u, 0u};
      std::copy(code.begin(), code.end(), memory.words.begin() + 0x1000 / 4);
      for (unsigned stage = 0; stage < 6; ++stage) {
        const bool configured_little = little ^ (stage == 2 || stage == 3);
        cpu.write_control(Config, configured_little ? 0x70066460 : 0x7006e460);
        cpu.write_control(Status, 0x30000000);
        if (stage == 1 || stage == 4) {
          cpu.state().gpr[2] = 0xffffffffa0001000ull ^ (configured_little ? 4u : 0u);
          cpu.state().gpr[9] = i(9, 0, 6, stage == 1 ? 31 : 7);
          cpu.execute(i(43, 2, 9, 0));
        }
        if (stage == 3 || stage == 5) {
          cpu.state().gpr[3] = 0xffffffff80001000ull;
          cpu.execute(i(47, 3, 16, 0));
        }
        for (unsigned reg : {6u, 7u, 8u})
          cpu.state().gpr[reg] = 0;
        cpu.state().gpr[31] = 0xffffffff80003000ull;
        cpu.set_pc(0xffffffff80001000ull);
        cpu.write_control(Count, 0);
        cpu.write_control(Compare, 0xffffffff);
        const auto start = cpu.state().clocks;
        const bool executed = stage == 0 || stage == 3 || stage == 5;
        equal(interpreted ? cpu.run_interpreted_block(start + 10000) : cpu.run_block(start + 10000),
              executed);
        const unsigned first = stage == 0 ? 17 : stage == 3 ? 31 : 7;
        equal(cpu.state().clocks - start, executed ? 104 : 0);
        equal(cpu.read_control(Count), executed ? 26 : 0);
        equal(cpu.state().gpr[6], executed ? first : 0);
        equal(cpu.state().gpr[7], executed ? first + 29 : 0);
        equal(cpu.state().gpr[8], executed ? 1 : 0);
        equal(cpu.state().pc, executed ? 0xffffffff80003000ull : 0xffffffff80001000ull);
        equal(cpu.read_control(Cause), 0);
        equal(cpu.read_control(Epc), 0);
      }
    }
}

} // namespace

void native_byte_order_tests() {
  capture_order();
  hit_order();
  ram_boundary_order();
  stale_capture_order();
}

} // namespace test
