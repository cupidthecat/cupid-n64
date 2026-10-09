#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct Case {
  std::uint32_t instruction;
  unsigned scalar_clocks, native_clocks;
  std::uint64_t source = 5, target = 3;
  bool warm_data = false;
  std::uint64_t a = 0x3f800000, b = 0x3f800000;
};

constexpr std::array<Case, 41> cases{
    {{0x24020007, 2, 0},
     {0x3c028032, 2, 0},
     {0x00001010, 2, 0},
     {0x00851020, 2, 0},
     {0x0085102c, 2, 0},
     {0x00850018, 10, 0},
     {0x0085001c, 16, 0},
     {0x0085001a, 74, 0},
     {0x0085001e, 138, 0},
     {0x0085001a, 74, 146, 0x5ull, 0, false, 0x3f800000ull, 0x3f800000ull},
     {0x0085001e, 138, 274, 0x5ull, 0, false, 0x3f800000ull, 0x3f800000ull},
     {0x8c820000, 82, 82, 0xffffffff80000800ull, 3, false, 0x3f800000ull, 0x3f800000ull},
     {0x8c820000, 4, 2, 0xffffffff80000800ull, 3, true, 0x3f800000ull, 0x3f800000ull},
     {0x8c820000, 2, 2, 0xffffffffa0000800ull, 3, false, 0x3f800000ull, 0x3f800000ull},
     {0xac820000, 82, 82, 0xffffffff80000800ull, 3, false, 0x3f800000ull, 0x3f800000ull},
     {0xac820000, 4, 2, 0xffffffff80000800ull, 3, true, 0x3f800000ull, 0x3f800000ull},
     {0xac820000, 2, 2, 0xffffffffa0000800ull, 3, false, 0x3f800000ull, 0x3f800000ull},
     {0x46052080, 6, 0},
     {0x46252080, 6, 0, 0x5ull, 3, false, 0x3ff0000000000000ull, 0x3ff0000000000000ull},
     {0x46052080, 6, 14, 0x5ull, 3, false, 0x7fc00001ull, 0x3f800000ull},
     {0x46252080, 6, 10, 0x5ull, 3, false, 0x7ff8000000000001ull, 0x3ff0000000000000ull},
     {0x46052032, 2, 0},
     {0x46252032, 2, 0, 0x5ull, 3, false, 0x3ff0000000000000ull, 0x3ff0000000000000ull},
     {0x40026800, 2, 2},
     {0x40024800, 2, 2},
     {0x40026000, 2, 2},
     {0x4442f800, 2, 2},
     {0xbc9f0000, 4, 4},
     {0x241c0007, 2, 0},
     {0x27bd0004, 2, 0},
     {0x46002086, 2, 0},
     {0x46202086, 2, 0},
     {0x44022000, 2, 0},
     {0x44222000, 2, 0},
     {0x44821000, 2, 0},
     {0x44a21000, 2, 0},
     {0x40426800, 2, 0},
     {0x40c26800, 2, 0},
     {0x41020001, 2, 0},
     {0x00850034, 2, 0},
     {0x00850036, 2, 2}}};

void prepare(native_memory::CachedFixture &f, const Case &row, bool warm) {
  f.memory.words.fill(0);
  f.memory.words[0x180 / 4] = row.instruction;
  f.memory.words[0x184 / 4] = r(8, 31, 0, 0);
  f.memory.words[0x800 / 4] = 0x11223344;
  f.memory.put(0x800, 4, 0x11223344);
  f.cpu.write_control(Status, 0x34000400);
  f.cpu.state().gpr[2] = 0x12345678;
  f.cpu.state().gpr[4] = row.source;
  f.cpu.state().gpr[5] = row.target;
  f.cpu.state().gpr[29] = 0xffffffffa4001ff0;
  f.cpu.state().gpr[31] = 0xffffffff80001000;
  f.cpu.state().hi = 0x55667788;
  f.cpu.state().fpr[2] = 0x0123456789abcdef;
  f.cpu.state().fpr[4] = row.a;
  f.cpu.state().fpr[5] = row.b;
  if (row.warm_data)
    f.cpu.execute(i(35, 4, 1, 0));
  if (warm) {
    f.cpu.state().gpr[1] = 0xffffffff80000180;
    f.cpu.execute(i(47, 1, 20, 0));
  }
  f.cpu.set_pc(0xffffffff80001000);
  f.cpu.write_control(Status, 0x34000401);
  f.cpu.set_interrupt(2, true);
  f.cpu.step();
  equal(f.cpu.state().pc, 0xffffffff80000180);
  equal(f.cpu.read_control(Epc), 0xffffffff80001000);
  f.cpu.write_control(Count, 0);
}

} // namespace

void native_interrupt_entry_tests() {
#if defined(_M_X64) || defined(__x86_64__)
  for (const auto &row : cases)
    for (bool warm : {false, true})
      for (bool expired : {false, true}) {
        native_memory::CachedFixture scalar, native;
        for (auto *f : {&scalar, &native}) {
          prepare(*f, row, warm);
          f->cpu.write_control(Compare, expired ? 0 : 0xffffffff);
        }
        const auto scalar_start = scalar.cpu.state().clocks;
        const auto native_start = native.cpu.state().clocks;
        scalar.cpu.step();
        equal(native.cpu.run_block(expired ? native_start : native_start + 4096), true);
        const auto scalar_clocks = row.scalar_clocks + (warm ? 0 : 96);
        const auto native_clocks = row.native_clocks + (warm ? 0 : 96);
        equal(scalar.cpu.state().clocks - scalar_start, scalar_clocks);
        equal(native.cpu.state().clocks - native_start, native_clocks);
        equal(scalar.cpu.read_control(Count), scalar_clocks / 4);
        equal(native.cpu.read_control(Count), native_clocks / 4);
        native_memory::compare(native, scalar, false);
        const bool store = row.instruction == i(43, 4, 2, 0);
        if (store && row.source == 0xffffffff80000800ull)
          for (auto *f : {&scalar, &native}) {
            f->cpu.execute(i(47, 4, 21, 0));
            equal(f->memory.words[0x800 / 4], 0x12345678);
          }
      }
#endif
}

} // namespace test
