#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

bool unused_function(unsigned function) {
  return function != 1 && function != 2 && function != 6 && function != 8 && function != 0x10 &&
         function != 0x18;
}

} // namespace

void control_dispatch_tests() {
  for (auto status : {0u, 8u, 16u, 0x48u, 0x30u, 2u, 4u, 0x00400010u, 0x10000000u, 0x10000008u,
                      0x10000010u, 0x10000048u, 0x10000030u}) {
    Fixture f;
    for (unsigned format = 16; format < 32; ++format) {
      for (unsigned function = 0; function < 64; ++function) {
        if (!unused_function(function))
          continue;
        for (bool delay : {false, true}) {
          f.cpu.power();
          f.cpu.set_pc(0xffffffffa0001000);
          f.cpu.write_control(Status, status);
          f.cpu.write_control(Epc, 0x12345678);
          f.cpu.state().gpr[2] = 0x1122334455667788;
          f.cpu.state().fcr31 = 0x1234;
          if (delay)
            f.cpu.execute(i(4, 0, 0, 3));
          const auto clocks = f.cpu.state().clocks;
          f.cpu.execute(c(format, 2, Status) | function);
          equal(f.cpu.state().pc, delay ? 0xffffffffa0001010 : 0xffffffffa0001004);
          equal(f.cpu.state().clocks, clocks + 2);
          equal(f.cpu.read_control(Status), status);
          equal(f.cpu.read_control(Cause), 0);
          equal(f.cpu.read_control(Epc), 0x12345678);
          equal(f.cpu.state().gpr[2], 0x1122334455667788);
          equal(f.cpu.state().fcr31, 0x1234);
          equal(f.cpu.in_delay_slot(), false);
        }
      }
    }
    for (unsigned format : {3u, 7u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 31u}) {
      for (bool delay : {false, true}) {
        f.cpu.power();
        f.cpu.set_pc(0xffffffffa0001000);
        f.cpu.write_control(Status, status);
        f.cpu.write_control(Epc, 0x12345678);
        if (delay)
          f.cpu.execute(i(4, 0, 0, 3));
        f.cpu.execute(c(format, 2, Status) | (format >= 16 ? 0x10 : 0));
        equal(f.exception(), 10);
        equal((f.cpu.read_control(Cause) >> 28) & 3, 0);
        equal(f.cpu.read_control(Epc), status & 2 ? 0x12345678 : 0xffffffffa0001000);
        equal((f.cpu.read_control(Cause) >> 31) & 1, delay && !(status & 2));
        equal(f.cpu.state().pc, status & 0x00400000 ? 0xffffffffbfc00380 : 0xffffffff80000180);
      }
    }
    for (unsigned format : {2u, 6u, 8u}) {
      f.cpu.power();
      f.cpu.set_pc(0xffffffffa0001000);
      f.cpu.write_control(Status, status);
      f.cpu.execute(c(format, 2, Status));
      equal(f.cpu.read_control(Cause), 0);
      equal(f.cpu.read_control(Status), status);
      equal(f.cpu.state().pc, 0xffffffffa0001004);
    }
  }
  for (auto status : {8u, 16u, 0x48u, 0x30u}) {
    for (const auto instruction : {c(0, 2, TagLo), c(1, 2, TagLo), c(4, 2, TagLo), c(5, 2, TagLo),
                                   co(1), co(2), co(6), co(8), co(0x18)}) {
      Fixture f;
      f.cpu.write_control(Status, status);
      f.cpu.execute(instruction);
      equal(f.exception(), 11);
      equal((f.cpu.read_control(Cause) >> 28) & 3, 0);
      equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    }
  }
  for (auto status : {0x10000008u, 0x10000010u, 0x10000048u, 0x10000030u}) {
    for (unsigned format : {0u, 1u, 4u, 5u}) {
      Fixture f;
      f.cpu.write_control(Status, status);
      f.cpu.state().gpr[2] = 0x1122334455667788;
      f.cpu.execute(c(format, 2, TagLo));
      const bool wide = format & 1;
      const bool extended = status & 0x60;
      equal(f.exception(), wide && !extended ? 10 : 0);
      if (format >= 4)
        equal(f.cpu.read_control(TagLo), wide && !extended ? 0 : 0x55667788);
    }
  }
  for (bool interpreted : {false, true}) {
    for (bool delay : {false, true}) {
      for (auto status : {0u, 0x80u}) {
        for (const auto instruction : {co(0), co(0x20), co(0x3f), c(3, 2, Status), co(0x10)}) {
          native_memory::CachedFixture f;
          f.cpu.write_control(Status, status);
          f.cpu.write_control(Epc, 0x12345678);
          f.cpu.state().gpr[2] = 0x1122334455667788;
          f.code(0, delay ? (2u << 26) | (0x80001010 >> 2 & 0x03ffffff) : instruction, false);
          f.code(4, delay ? instruction : i(9, 0, 28, 1), false);
          const auto target = f.cpu.state().clocks + 4096;
          equal(interpreted ? f.cpu.run_interpreted_block(target) : f.cpu.run_block(target), true);
          const bool invalid = instruction == c(3, 2, Status) || instruction == co(0x10);
          equal((f.cpu.read_control(Cause) >> 2) & 31, invalid ? 10 : 0);
          equal((f.cpu.read_control(Cause) >> 28) & 3, 0);
          equal(f.cpu.read_control(Epc), invalid ? 0xffffffff80001000 : 0x12345678);
          equal(f.cpu.state().pc, invalid ? 0xffffffff80000180
                                  : delay ? 0xffffffff80001010
                                          : 0xffffffff80001008);
          equal(f.cpu.read_control(Status), invalid ? status | 2 : status);
          equal(f.cpu.state().gpr[2], 0x1122334455667788);
          equal(f.cpu.state().gpr[28], !invalid && !delay ? 1 : 0);
          equal((f.cpu.read_control(Cause) >> 31) & 1, invalid && delay);
        }
      }
    }
  }
}

} // namespace test
