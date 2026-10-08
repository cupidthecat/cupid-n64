#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr unsigned operations[] = {26, 27, 34, 38, 42, 46, 44, 45, 48, 52, 56, 60};

void batches() {
  for (unsigned operation : operations) {
    const unsigned bytes = operation == 26 || operation == 27 || operation == 44 ||
                                   operation == 45 || operation == 52 || operation == 60
                               ? 8
                               : 4;
    const bool partial = operation < 48;
    const bool conditional = operation == 56 || operation == 60;
    for (unsigned before = 0; before < 8; ++before) {
      for (unsigned access = 0; access < 3; ++access) {
        for (unsigned offset = 0; offset < 8; ++offset) {
          for (bool linked : {false, true}) {
            if (linked && !conditional)
              continue;
            for (bool delay : {false, true}) {
              native_memory::CachedFixture actual;
              native_memory::CachedFixture expected;
              for (auto *fixture : {&actual, &expected}) {
                fixture->cpu.write_control(Status, 0x34000000);
                fixture->cpu.state().gpr[2] =
                    access == 0 ? 0xffffffffa0000200ull : 0xffffffff80000200ull;
                for (unsigned word = 0; word < 4; ++word) {
                  const auto value = word == 0   ? 0x10203040u
                                     : word == 1 ? 0x50607080u
                                     : word == 2 ? 0x90a0b0c0u
                                                 : 0u;
                  fixture->memory.words[(0x200 + word * 4) / 4] = value;
                  fixture->memory.put(0x200 + word * 4, 4, value);
                }
                if (access == 2)
                  fixture->cpu.execute(i(35, 2, 9, 0));
                if (linked)
                  fixture->cpu.execute(i(48, 2, 9, 0));
                fixture->cpu.state().gpr[8] = 0x89abcdef01234567;
                fixture->cpu.state().gpr[31] = 0xffffffff80003000;
                for (unsigned word = 0; word < before; ++word)
                  fixture->code(word * 4, i(9, 6, 6, 1), false);
                unsigned word = before;
                if (delay) {
                  fixture->code(word++ * 4, r(8, 31, 0, 0), false);
                  fixture->code(word++ * 4, i(operation, 2, 8, offset), false);
                } else {
                  fixture->code(word++ * 4, i(operation, 2, 8, offset), false);
                  fixture->code(word++ * 4, r(8, 31, 0, 0), false);
                  fixture->code(word++ * 4, 0, false);
                }
                fixture->cpu.set_pc(0xffffffff80001000);
                fixture->cpu.write_control(Count, 0);
                fixture->cpu.write_control(Compare, 0xffffffff);
              }
              const bool aligned = !(offset & (bytes - 1));
              const bool fault = !partial && !aligned && (!conditional || linked);
              const unsigned fetched = before + (fault ? unsigned(delay) + 1 : delay ? 2 : 3);
              unsigned clocks = 96 * ((fetched + 7) / 8) + before * 2;
              const bool hit = (access == 2 || (linked && access != 0)) && (partial || aligned);
              if (fault)
                clocks += 4;
              else if (hit)
                clocks += delay ? 6 : 8;
              else {
                unsigned memory_clocks = 0;
                if (access != 0 && (!conditional || linked)) {
                  memory_clocks = 80;
                  if (partial && operation >= 40) {
                    const unsigned lane = offset & (bytes - 1);
                    const unsigned length =
                        operation == 42 || operation == 44 ? bytes - lane : lane + 1;
                    memory_clocks += 2 * (std::popcount(length) - 1);
                  }
                }
                clocks += memory_clocks + (delay ? 2 : before * 2 + 8);
              }
              const auto start = actual.cpu.state().clocks;
              equal(actual.cpu.run_block(start + 10000), true);
              equal(expected.cpu.run_interpreted_block(expected.cpu.state().clocks + 10000), true);
              equal(actual.cpu.state().clocks - start, clocks);
              equal(actual.cpu.read_control(Count), clocks / 4);
              equal(actual.cpu.state().gpr[6], before);
              equal(actual.cpu.state().gpr[8], expected.cpu.state().gpr[8]);
              equal(actual.cpu.state().pc, expected.cpu.state().pc);
              for (unsigned control : {unsigned(Cause), unsigned(Epc), unsigned(LlAddr)})
                equal(actual.cpu.read_control(control), expected.cpu.read_control(control));
              for (unsigned offset : {0u, 8u}) {
                for (auto *fixture : {&actual, &expected})
                  fixture->cpu.execute(i(55, 2, 10, offset));
                equal(actual.cpu.state().gpr[10], expected.cpu.state().gpr[10]);
              }
            }
          }
        }
      }
    }
  }
}

void registers() {
  for (bool little : {false, true}) {
    for (unsigned operation : operations) {
      for (unsigned target : {0u, 1u, 2u}) {
        for (bool hit : {false, true}) {
          for (unsigned offset = 0; offset < 8; ++offset) {
            for (bool delay : {false, true}) {
              native_memory::CachedFixture actual;
              native_memory::CachedFixture expected;
              for (auto *fixture : {&actual, &expected}) {
                fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
                fixture->cpu.state().gpr[1] = 0xffffffff80000200;
                fixture->cpu.state().gpr[2] = 0x89abcdef01234567;
                if (hit)
                  fixture->cpu.execute(i(35, 1, 9, 0));
                if (operation == 56 || operation == 60)
                  fixture->cpu.execute(i(48, 1, 9, 0));
                fixture->cpu.state().gpr[1] += offset + 7;
                fixture->cpu.state().gpr[31] = 0xffffffff80003000;
                if (delay) {
                  fixture->code(0, r(8, 31, 0, 0), little);
                  fixture->code(4, i(operation, 1, target, 0xfff9), little);
                } else {
                  fixture->code(0, i(operation, 1, target, 0xfff9), little);
                  fixture->code(4, r(8, 31, 0, 0), little);
                  fixture->code(8, 0, little);
                }
                fixture->cpu.set_pc(0xffffffff80001000);
              }
              equal(actual.cpu.run_block(10000), true);
              equal(expected.cpu.run_interpreted_block(10000), true);
              for (unsigned reg = 0; reg < 32; ++reg)
                equal(actual.cpu.state().gpr[reg], expected.cpu.state().gpr[reg]);
              equal(actual.cpu.state().pc, expected.cpu.state().pc);
              for (unsigned control : {unsigned(Cause), unsigned(Epc), unsigned(LlAddr)})
                equal(actual.cpu.read_control(control), expected.cpu.read_control(control));
              for (auto *fixture : {&actual, &expected}) {
                fixture->cpu.state().gpr[4] = 0xffffffff80000200;
                for (unsigned offset : {0u, 4u, 8u, 12u})
                  fixture->cpu.execute(i(35, 4, 10, offset));
                fixture->cpu.execute(i(47, 4, 21, 0));
              }
              equal(actual.cpu.state().gpr[10], expected.cpu.state().gpr[10]);
              for (unsigned word = 0; word < 4; ++word)
                equal(actual.memory.words[0x200 / 4 + word],
                      expected.memory.words[0x200 / 4 + word]);
            }
          }
        }
      }
    }
  }
}

} // namespace

void native_merge_linked_timing_tests() {
  batches();
  registers();
}

} // namespace test
