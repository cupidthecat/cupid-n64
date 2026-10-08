#include "../fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_halt_context_tests() {
  for (bool delay : {false, true}) {
    for (unsigned kind = 0; kind < 4; ++kind) {
      const bool command = kind & 1, vector_mode = kind & 2;
      for (unsigned reg = 1; reg < 31; ++reg) {
        for (unsigned before = 0; before < 8; ++before) {
          RspFixture f;
          f.rsp.state().gpr[1] = 2;
          f.rsp.state().gpr[reg] = 7;
          if (command)
            f.rsp.state().gpr[1] = 2;
          auto expected = f.rsp.state().gpr;
          if (vector_mode)
            for (unsigned lane = 0; lane < 8; ++lane)
              f.rsp.state().vectors[reg].lanes[lane] =
                  static_cast<std::uint16_t>(0x8000 + reg * 31 + lane * 123);
          auto vectors = f.rsp.state().vectors;
          RspVector accumulator;
          const auto load = vector_mode ? vector_memory(false, 4, reg, 0, 0, 0) : i(35, 0, reg, 0);
          for (unsigned word = 0; word < before; ++word)
            f.rsp.write_local(0x1000 + word * 4, 4, i(9, 6, 6, 1));
          const auto halt = before + unsigned(delay);
          if (delay)
            f.rsp.write_local(0x1000 + before * 4, 4, 0x08000080 | (before & 1));
          f.rsp.write_local(0x1000 + halt * 4, 4, command ? c(4, 1, 4) : 13);
          f.rsp.write_local(0x1004 + halt * 4, 4, load);
          f.rsp.write_local(0x1008 + halt * 4, 4, 0x08000000);
          f.rsp.write_local(0x100c + halt * 4, 4, 0);
          const auto instruction = vector_mode ? vector(42, 31, reg, 0)
                                   : command   ? i(43, 0, reg, 0x100)
                                               : i(9, reg, 31, 1);
          f.rsp.write_local(0x1100, 4, instruction);
          f.rsp.write_local(0x1104, 4, 0x08000040);
          f.rsp.write_local(0x1108, 4, load);
          f.rsp.write_local(0, 4, 41);
          f.rsp.write_status(0, 0);
          f.rsp.write_io(16, 5);
          std::uint32_t stored = 0;
          for (unsigned stage = 0; stage < 5; ++stage) {
            if (stage == 1) {
              f.rsp.write_status(0, 0x100);
              f.rsp.write_io(16, 5);
            }
            if (stage == 3)
              f.rsp.write_local(0x1100, 4, instruction);
            if (!stage)
              expected[6] += before;
            else if (vector_mode) {
              accumulator = vectors[31] = vectors[reg];
              vectors[reg] = {};
              vectors[reg].lanes[1] = 41;
            } else {
              if (command)
                stored = expected[reg];
              else
                expected[31] = expected[reg] + 1;
              expected[reg] = 41;
            }
            f.rsp.advance(static_cast<std::uint32_t>(f.rsp.clocks() + 1));
            const auto restart = !delay        ? command ? 11 : 17
                                 : !command    ? 11
                                 : vector_mode ? (before & 1 ? 11 : 8)
                                               : 14;
            const auto clocks = !stage ? before * 3 + (delay ? 8 : 2) : stage >= 3 ? 14 : restart;
            const auto stopped = delay ? 0x200 + (before & 1) * 4 : command ? 0 : (before + 1) * 4;
            const auto pc = stage ? 0x100 : stopped;
            equal(f.rsp.clocks(), clocks);
            equal(f.rsp.pc(), pc);
            equal(f.rsp.status().halted, stage == 0);
            equal(f.rsp.status().broken, stage == 0 && !command);
            for (unsigned index = 0; index < 32; ++index) {
              equal(f.rsp.state().gpr[index], expected[index]);
              for (unsigned lane = 0; lane < 8; ++lane)
                equal(f.rsp.state().vectors[index].lanes[lane], vectors[index].lanes[lane]);
            }
            for (unsigned lane = 0; lane < 8; ++lane)
              equal(f.rsp.state().accumulator.get(lane), accumulator.lanes[lane]);
            equal(f.rsp.read_local(0, 4), 41);
            equal(f.rsp.read_local(0x100, 4), stored);
          }
        }
      }
    }
  }
}

} // namespace test
