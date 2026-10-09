#include "../../native_memory_fixture.hpp"
#include "cases.hpp"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace test {
using namespace cupid::n64;

void native_fpu_timing_tests() {
#if defined(_M_X64) || defined(__x86_64__)
  for (const auto &row : native_fpu::cases)
    for (unsigned mode : {0x30000000u, 0x34000000u})
      for (unsigned position = 0; position < 3; ++position)
        for (bool delay : {false, true})
          for (bool warm : {false, true}) {
            native_memory::CachedFixture f;
            constexpr unsigned offsets[]{0, 1, 7};
            const auto before = offsets[position];
            f.cpu.write_control(Status, mode);
            f.cpu.state().gpr[31] = 0xffffffff80003000;
            unsigned word = 0;
            for (; word < before; ++word)
              f.code(word * 4, i(9, 10, 10, 1));
            const auto instruction =
                0x44000000 | (row.format << 21) | (5 << 16) | (3 << 11) | (7 << 6) | row.operation;
            if (delay) {
              f.code(word++ * 4, r(8, 31, 0, 0));
              f.code(word++ * 4, instruction);
            } else {
              f.code(word++ * 4, instruction);
              f.code(word++ * 4, r(8, 31, 0, 0));
              f.code(word++ * 4, 0);
            }
            if (warm)
              for (unsigned line = 0; line < (word + 7) / 8; ++line) {
                f.cpu.state().gpr[1] = 0xffffffff80001000ull + line * 32;
                f.cpu.execute(i(47, 1, 20, 0));
              }
            f.cpu.set_pc(0xffffffff80001000);
            f.cpu.write_control(Count, 0);
            f.cpu.write_control(Compare, 0xffffffff);
            f.cpu.state().fcr31 = row.control | 0x0083f024;
            for (unsigned n = 0; n < 32; ++n)
              f.cpu.state().fpr[n] = 0x0123456789abcdefull ^ n;
            f.cpu.state().fpr[mode & 0x04000000 ? 3 : 2] = row.a;
            f.cpu.state().fpr[5] = row.b;
            const auto start = f.cpu.state().clocks;
            const auto host = _mm_getcsr();
            _mm_setcsr(0x7fbf);
            equal(f.cpu.run_block(start + 10000), true);
            equal(_mm_getcsr(), 0x7fbf);
            _mm_setcsr(host);
            const auto fetched = before + (row.fault ? 1 + unsigned(delay) : 3 - unsigned(delay));
            const auto clocks = before * 2 + row.clocks[position * 2 + unsigned(delay)] +
                                (warm ? 0 : 96 * ((fetched + 7) / 8));
            equal(f.cpu.state().clocks - start, clocks);
            equal(f.cpu.read_control(Count), clocks / 4);
            equal(f.cpu.state().fpr[7], row.result);
            equal(f.cpu.state().fpr[6], 0x0123456789abcde9);
            equal(f.cpu.state().fcr31, row.fcr31);
            equal(f.cpu.state().gpr[10], before);
            equal(f.cpu.state().pc, row.fault ? 0xffffffff80000180ull : 0xffffffff80003000ull);
            equal(f.cpu.read_control(Cause), row.fault ? 0x3cu | (delay ? 0x80000000u : 0) : 0);
            equal(f.cpu.read_control(Epc), row.fault ? 0xffffffff80001000ull + before * 4 : 0);
          }
#endif
}

} // namespace test
