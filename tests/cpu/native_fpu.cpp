#include "native_memory_fixture.hpp"
#include <array>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace test {
using namespace cupid::n64;
using native_memory::CachedFixture;
namespace {

std::uint32_t fp(unsigned format, unsigned operation, unsigned source = 2, unsigned target = 4,
                 unsigned dest = 6) {
  return 0x44000000 | (format << 21) | (target << 16) | (source << 11) | (dest << 6) | operation;
}

void compare_state(CachedFixture &actual, CachedFixture &expected) {
  const auto &a = actual.cpu.state();
  const auto &b = expected.cpu.state();
  equal(a.pc, b.pc);
  equal(a.fcr31, b.fcr31);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(a.gpr[reg], b.gpr[reg]);
    equal(a.fpr[reg], b.fpr[reg]);
    if (reg != Count)
      equal(actual.cpu.read_control(reg), expected.cpu.read_control(reg));
  }
  equal(actual.memory.transfers.size(), expected.memory.transfers.size());
}

void run(CachedFixture &actual, CachedFixture &expected) {
#if defined(_M_X64) || defined(__x86_64__)
  const auto saved = _mm_getcsr();
  _mm_setcsr(0x7fbf);
#endif
  const auto limit = actual.cpu.state().clocks;
  equal(actual.cpu.run_block(limit), true);
#if defined(_M_X64) || defined(__x86_64__)
  equal(_mm_getcsr(), 0x7fbf);
  _mm_setcsr(saved);
#endif
  equal(expected.cpu.run_interpreted_block(limit), true);
  compare_state(actual, expected);
}

void program(CachedFixture &fixture, std::uint32_t instruction, bool little, unsigned layout = 0) {
  fixture.cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
  fixture.code(0, layout == 1 ? i(4, 0, 0, 2) : layout == 2 ? 0 : instruction, little);
  fixture.code(4, layout ? instruction : 0, little);
  fixture.code(8, 0x08000410, little);
  fixture.code(12, 0, little);
  fixture.code(64, 0x08000410, little);
  fixture.code(68, 0, little);
}

void state(CachedFixture &fixture, unsigned mode, unsigned control, std::uint64_t a,
           std::uint64_t b, unsigned source = 2, unsigned target = 4) {
  fixture.cpu.write_control(Status, mode);
  fixture.cpu.state().fcr31 = control | 0x0003f000 | ((a & 1) ? 0x7c : 0);
  fixture.cpu.set_pc(0xffffffff80001000);
  for (unsigned reg = 0; reg < 32; ++reg) {
    fixture.cpu.state().fpr[reg] = 0x1122334455667788ull ^ (0x1020304050607080ull * reg);
    fixture.cpu.state().gpr[reg] = reg ? 0xfedcba9876543210ull + reg : 0;
  }
  fixture.cpu.state().fpr[(mode & 0x04000000) ? source : source & ~1u] = a;
  fixture.cpu.state().fpr[target] = b;
}

void transfers() {
  for (bool little : {false, true}) {
    for (unsigned format : {0u, 1u, 4u, 5u, 16u, 17u}) {
      for (unsigned source : {0u, 1u, 2u, 3u, 30u, 31u}) {
        for (unsigned dest : {0u, 1u, 2u, 31u}) {
          CachedFixture actual, expected;
          const auto instruction =
              format < 16 ? fp(format, 0, source, dest, 0) : fp(format, 6, source, 0, dest);
          program(actual, instruction, little);
          program(expected, instruction, little);
          for (unsigned mode :
               {0x30000000u, 0x34000000u, 0x10000000u, 0x14000000u, 0x34000000u, 0x30000000u}) {
            for (auto *fixture : {&actual, &expected})
              state(*fixture, mode, 0x01800003, 0x812345679abcdef0, 0, source, 8);
            run(actual, expected);
          }
        }
      }
    }
  }
}

void arithmetic() {
  constexpr std::array<std::uint64_t, 20> single{
      0,          0x80000000, 1,          0x007fffff, 0x00800000, 0x80800000, 0x3f800000,
      0xbf800000, 0x3f000000, 0x3f800001, 0x40000000, 0x40400000, 0x7f7fffff, 0xff7fffff,
      0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001, 0x00800001, 0x3eaaaaab};
  constexpr std::array<std::uint64_t, 20> dual{0,
                                               0x8000000000000000,
                                               1,
                                               0x000fffffffffffff,
                                               0x0010000000000000,
                                               0x8010000000000000,
                                               0x3ff0000000000000,
                                               0xbff0000000000000,
                                               0x3fe0000000000000,
                                               0x3ff0000000000001,
                                               0x4000000000000000,
                                               0x4008000000000000,
                                               0x7fefffffffffffff,
                                               0xffefffffffffffff,
                                               0x7ff0000000000000,
                                               0xfff0000000000000,
                                               0x7ff8000000000000,
                                               0x7ff0000000000001,
                                               0x0010000000000001,
                                               0x3fd5555555555555};
  for (unsigned format : {16u, 17u}) {
    const auto &values = format == 16 ? single : dual;
    for (unsigned operation :
         {0u,    1u,    2u,    3u,    4u,    5u,    7u,    0x30u, 0x31u, 0x32u, 0x33u, 0x34u,
          0x35u, 0x36u, 0x37u, 0x38u, 0x39u, 0x3au, 0x3bu, 0x3cu, 0x3du, 0x3eu, 0x3fu}) {
      CachedFixture actual, expected;
      program(actual, fp(format, operation), false);
      program(expected, fp(format, operation), false);
      for (unsigned control : {0u, 1u, 2u, 3u, 0x01000000u, 0x01000001u, 0x01000002u, 0x01000003u,
                               0x80u, 0x100u, 0x200u, 0x400u, 0x800u, 0xf80u}) {
        for (auto a : values) {
          for (auto b : values) {
            for (auto *fixture : {&actual, &expected})
              state(*fixture, 0x34000000, control, a, b);
            run(actual, expected);
            if (failures)
              return;
          }
        }
      }
    }
  }
}

void conversions() {
  constexpr std::array<std::uint64_t, 24> single{
      0,          0x80000000, 1,          0x007fffff, 0x00800000, 0x3f000000,
      0xbf000000, 0x3fc00000, 0xbfc00000, 0x40200000, 0xc0200000, 0x4effffff,
      0xceffffff, 0x4f000000, 0xcf000000, 0x59ffffff, 0xd9ffffff, 0x5a000000,
      0xda000000, 0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001, 0x3eaaaaab};
  constexpr std::array<std::uint64_t, 24> dual{0,
                                               0x8000000000000000,
                                               1,
                                               0x000fffffffffffff,
                                               0x0010000000000000,
                                               0x3fe0000000000000,
                                               0xbfe0000000000000,
                                               0x3ff8000000000000,
                                               0xbff8000000000000,
                                               0x4004000000000000,
                                               0xc004000000000000,
                                               0x41dfffffffe00000,
                                               0xc1dfffffffe00000,
                                               0x41dffffffff00000,
                                               0xc1e0000000000000,
                                               0x433fffffffffffff,
                                               0xc33fffffffffffff,
                                               0x4340000000000000,
                                               0xc340000000000000,
                                               0x7ff0000000000000,
                                               0xfff0000000000000,
                                               0x7ff8000000000000,
                                               0x7ff0000000000001,
                                               0x36a0000000000000};
  constexpr std::array<std::uint64_t, 14> integer{0,
                                                  1,
                                                  ~0ull,
                                                  0x7fffffff,
                                                  0x80000000,
                                                  0xffffffff80000000,
                                                  0x7fffffffffffffff,
                                                  0x8000000000000000,
                                                  0x007fffffffffffff,
                                                  0x0080000000000000,
                                                  0xff80000000000000,
                                                  0xff7fffffffffffff,
                                                  0x01000001,
                                                  0x0020000000000001};
  for (unsigned format : {16u, 17u, 20u, 21u}) {
    for (unsigned operation : {8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 0x20u, 0x21u, 0x24u, 0x25u}) {
      CachedFixture actual, expected;
      program(actual, fp(format, operation), false);
      program(expected, fp(format, operation), false);
      const std::span<const std::uint64_t> values =
          format >= 20 ? std::span<const std::uint64_t>(integer)
                       : std::span<const std::uint64_t>(format == 16 ? single : dual);
      for (unsigned control :
           {0u, 1u, 2u, 3u, 0x01000000u, 0x01000002u, 0x01000003u, 0x80u, 0x100u, 0xf80u}) {
        for (auto value : values) {
          for (auto *fixture : {&actual, &expected})
            state(*fixture, 0x34000000, control, value, 0);
          run(actual, expected);
          if (failures)
            return;
        }
      }
    }
  }
}

void layouts() {
  for (bool little : {false, true}) {
    for (unsigned layout : {0u, 1u, 2u}) {
      for (unsigned format : {16u, 17u, 20u, 21u}) {
        for (unsigned operation : {0u, 2u, 3u, 4u, 6u, 0x20u, 0x21u, 0x24u, 0x32u}) {
          for (unsigned source : {0u, 1u, 2u, 3u}) {
            for (unsigned dest : {0u, 1u, 2u, 3u}) {
              CachedFixture actual, expected;
              program(actual, fp(format, operation, source, 3, dest), little, layout);
              program(expected, fp(format, operation, source, 3, dest), little, layout);
              for (unsigned mode : {0x30000000u, 0x34000000u, 0x10000000u, 0x34000000u}) {
                for (auto *fixture : {&actual, &expected})
                  state(*fixture, mode, 0x01000002, 0x3ff000003f800000, 0x3fe000003f000000, source,
                        3);
                run(actual, expected);
              }
            }
          }
        }
      }
    }
  }
}

void branches() {
  for (bool little : {false, true}) {
    for (unsigned condition = 0; condition < 4; ++condition) {
      for (unsigned compare_bit : {0u, 0x800000u}) {
        for (bool enabled : {false, true}) {
          CachedFixture actual, expected;
          for (auto *fixture : {&actual, &expected}) {
            state(*fixture, enabled ? 0x34000000 : 0x14000000, compare_bit, 0, 0);
            fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
            fixture->code(0, i(17, 8, condition, 3), little);
            fixture->code(4, fp(16, 6, 0, 0, 1), little);
            fixture->code(8, 0x08000410, little);
            fixture->code(12, 0, little);
            fixture->code(16, 0x08000410, little);
            fixture->code(20, 0, little);
          }
          run(actual, expected);
        }
      }
    }
  }
}

void randomized() {
  std::uint64_t random = 0x9f038df1823ac17bull;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return random;
  };
  for (unsigned format : {16u, 17u, 20u, 21u}) {
    for (unsigned operation :
         {0u,  1u,  2u,  3u,  4u,    5u,    7u,    8u,    9u,    10u,   11u,
          12u, 13u, 14u, 15u, 0x20u, 0x21u, 0x24u, 0x25u, 0x32u, 0x34u, 0x36u}) {
      CachedFixture actual, expected;
      program(actual, fp(format, operation), false);
      program(expected, fp(format, operation), false);
      for (unsigned sample = 0; sample < 1024; ++sample) {
        const auto a = next(), b = next();
        const auto control = static_cast<unsigned>(next()) & 0x01800f83;
        const auto mode = 0x30000000 | ((sample & 1) << 26);
        for (auto *fixture : {&actual, &expected})
          state(*fixture, mode, control, a, b);
        run(actual, expected);
        if (failures)
          return;
      }
    }
  }
}

void timing() {
  for (unsigned operation : {0u, 2u, 3u, 4u, 0x20u, 0x24u, 0x32u}) {
    for (unsigned count : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
      for (unsigned timer : {0u, 1u, 2u, 30u, 0xffffffffu}) {
        for (unsigned parity : {0u, 1u, 3u}) {
          CachedFixture actual, expected;
          std::uint64_t actual_limit = 0, expected_limit = 0;
          unsigned actual_sync = 0, expected_sync = 0;
          for (auto *fixture : {&actual, &expected}) {
            state(*fixture, 0x34000000, 0x01000000, 0x3ff8000000000000, 0x4008000000000000);
            fixture->code(0, fp(17, operation), false);
            fixture->code(4, i(4, 0, 0, 0xfffe), false);
            fixture->code(8, fp(17, operation), false);
            fixture->cpu.advance_clocks(parity);
            fixture->cpu.run_interpreted_block(0);
            fixture->cpu.write_control(Count, count);
            fixture->cpu.write_control(Compare, timer);
            fixture->cpu.write_control(Status, 0x34008001);
          }
          const auto actual_start = actual.cpu.state().clocks;
          const auto expected_start = expected.cpu.state().clocks;
          actual.cpu.connect_sync([&] {
            ++actual_sync;
            actual_limit = actual.cpu.state().clocks;
          });
          expected.cpu.connect_sync([&] {
            ++expected_sync;
            expected_limit = expected.cpu.state().clocks;
          });
          equal(actual.cpu.run_block(actual_limit), true);
          equal(expected.cpu.run_interpreted_block(expected_limit), true);
          compare_state(actual, expected);
          if (actual_sync) {
            equal(actual_limit >= actual_start && actual_limit <= actual.cpu.state().clocks, true);
            equal(expected_limit >= expected_start && expected_limit <= expected.cpu.state().clocks,
                  true);
          }
          equal(actual_sync, expected_sync);
        }
      }
    }
  }
}

void control_changes() {
  for (unsigned control : {0u, 1u, 2u, 3u, 0x01000000u, 0x01000002u, 0xf80u}) {
    CachedFixture actual, expected;
    for (auto *fixture : {&actual, &expected}) {
      state(*fixture, 0x34000000, 0, 0x3ff800003fc00000, 0x4008000040400000);
      fixture->cpu.state().gpr[10] = control;
      fixture->cpu.state().gpr[11] = 0x30000000;
      fixture->code(0, fp(16, 0x24, 2, 0, 7), false);
      fixture->code(4, fp(6, 0, 31, 10, 0), false);
      fixture->code(8, fp(16, 0x24, 2, 0, 8), false);
      fixture->code(12, c(4, 11, Status), false);
      fixture->code(16, fp(16, 0, 3, 4, 9), false);
      fixture->code(20, 0x08000400, false);
      fixture->code(24, 0, false);
    }
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
      for (unsigned block = 0; block < 3; ++block) {
        run(actual, expected);
        if ((actual.cpu.read_control(Status) & 2) != 0)
          break;
      }
      if ((actual.cpu.read_control(Status) & 2) != 0)
        break;
    }
  }
}

} // namespace

void native_fpu_tests() {
  transfers();
  arithmetic();
  conversions();
  layouts();
  branches();
  randomized();
  timing();
  control_changes();
}

} // namespace test
