#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr std::uint32_t operations[] = {
    0x40074800, 0x40274800, 0x40076000, 0x40076800, 0x00850030, 0x00850031, 0x00850032, 0x00850033,
    0x00850034, 0x00850036, 0x04880003, 0x04890003, 0x048a0003, 0x048b0003, 0x048c0003, 0x048e0003};
constexpr std::pair<std::uint64_t, std::uint64_t> operands[] = {
    {7, 3}, {3, 7}, {7, 7}, {0, 0}, {~6ull, 3}};

bool traps(std::uint32_t instruction, std::uint64_t a, std::uint64_t b) {
  const auto opcode = instruction >> 26;
  if (opcode == 1)
    b = static_cast<std::uint64_t>(
        std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  const auto condition = opcode == 1 ? (instruction >> 16) & 7 : instruction & 7;
  switch (condition) {
  case 0:
    return signed_value(a) >= signed_value(b);
  case 1:
    return a >= b;
  case 2:
    return signed_value(a) < signed_value(b);
  case 3:
    return a < b;
  case 4:
    return a == b;
  default:
    return a != b;
  }
}

void batches() {
  for (unsigned kind = 0; kind < 16; ++kind) {
    for (bool negative : {false, true}) {
      if (negative && kind < 10)
        continue;
      const auto instruction =
          negative ? (operations[kind] & 0xffff0000u) | 0xfffdu : operations[kind];
      for (const auto [a, b] : operands) {
        const bool fault = kind >= 4 && traps(instruction, a, b);
        for (unsigned before = 0; before < 8; ++before) {
          for (unsigned flow = 0; flow < 5; ++flow) {
            const bool delay = flow == 1;
            const bool memory = flow >= 2;
            native_memory::CachedFixture f;
            f.cpu.write_control(Status, 0x34000000);
            f.cpu.state().gpr[2] = flow == 2 ? 0xffffffffa0000200ull : 0xffffffff80000200ull;
            f.memory.words[0x200 / 4] = 0x1234;
            f.memory.put(0x200, 4, 0x1234);
            if (flow == 4)
              f.cpu.execute(i(35, 2, 8, 0));
            f.cpu.state().gpr[4] = a;
            f.cpu.state().gpr[5] = b;
            f.cpu.state().gpr[8] = 0;
            f.cpu.state().gpr[31] = 0xffffffff80003000;
            for (unsigned word = 0; word < before; ++word)
              f.code(word * 4, i(9, 6, 6, 1), false);
            unsigned word = before;
            if (delay) {
              f.code(word++ * 4, r(8, 31, 0, 0), false);
              f.code(word++ * 4, instruction, false);
            } else {
              f.code(word++ * 4, instruction, false);
              if (memory)
                f.code(word++ * 4, i(35, 2, 8, 0), false);
              f.code(word++ * 4, r(8, 31, 0, 0), false);
              f.code(word++ * 4, 0, false);
            }
            f.cpu.set_pc(0xffffffff80001000);
            f.cpu.write_control(Count, 0);
            f.cpu.write_control(Compare, 0xffffffff);
            const auto start = f.cpu.state().clocks;
            const unsigned fetched = fault ? before + unsigned(delay) + 1 : word;
            unsigned clocks = 96 * ((fetched + 7) / 8) + before * 2;
            if (fault)
              clocks += 2;
            else if (memory) {
              clocks += flow == 3 ? 90 : 10;
              if (kind >= 4 && flow != 4 && before < 7)
                clocks += before * 2 + 2;
            } else
              clocks += delay ? 4 : 6;
            equal(f.cpu.run_block(start + 10000), true);
            equal(f.cpu.state().clocks - start, clocks);
            equal(f.cpu.read_control(Count), clocks / 4);
            equal(f.cpu.state().gpr[6], before);
            equal(f.cpu.state().gpr[8], !fault && memory ? 0x1234 : 0);
            equal(f.cpu.state().pc, fault ? 0xffffffff80000180ull : 0xffffffff80003000ull);
            equal(f.cpu.read_control(Cause), fault ? 0x34u | (delay ? 0x80000000u : 0) : 0);
            equal(f.cpu.read_control(Epc), fault ? 0xffffffff80001000ull + before * 4 : 0);
            const auto read_clocks =
                96 * ((before + unsigned(delay) + 8) / 8) + before * 2 + unsigned(delay) * 2;
            equal(f.cpu.state().gpr[7], kind < 2 ? read_clocks / 4 : kind == 2 ? 0x34000000 : 0);
          }
        }
      }
    }
  }
}

} // namespace

void native_control_trap_timing_tests() {
  batches();
}

} // namespace test
