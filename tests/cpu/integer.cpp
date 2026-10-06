#include "../support/test.hpp"
#include <array>
#include <limits>

namespace test {
using namespace cupid::n64;

void integer_tests() {
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0x7fffffff;
    f.cpu.execute(i(9, 1, 2, 1));
    equal(f.cpu.state().gpr[2], 0xffffffff80000000);
    f.cpu.execute(i(9, 0, 0, 99));
    equal(f.cpu.state().gpr[0], 0);
    f.cpu.execute(i(0xf, 0, 2, 0x8000));
    equal(f.cpu.state().gpr[2], 0xffffffff80000000);
    f.cpu.execute(i(0xb, 1, 2, 0xffff));
    equal(f.cpu.state().gpr[2], 1);
    f.cpu.state().gpr[1] = 0xfedcba9876543210;
    f.cpu.execute(i(0xd, 1, 2, 0x8000));
    equal(f.cpu.state().gpr[2], 0xfedcba987654b210);
  }
  for (bool wide : {false, true}) {
    for (bool subtract : {false, true}) {
      Fixture f;
      const auto a = wide ? 0x7fffffffffffffffull : 0x7fffffffull;
      f.cpu.state().gpr[1] = a;
      f.cpu.state().gpr[2] = subtract ? ~0ull : 1;
      f.cpu.state().gpr[3] = 42;
      f.cpu.execute(r((wide ? 0x2c : 0x20) + (subtract ? 2 : 0), 1, 2, 3));
      equal(f.exception(), 12);
      equal(f.cpu.state().gpr[3], 42);
      equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
      equal(f.cpu.state().pc, 0xffffffff80000180);
    }
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0x1234567880000000;
    f.cpu.execute(r(3, 0, 1, 2, 1));
    equal(f.cpu.state().gpr[2], 0x40000000);
    f.cpu.state().gpr[1] = 0x8000000000000000;
    f.cpu.execute(r(0x3f, 0, 1, 2, 31));
    equal(f.cpu.state().gpr[2], ~0ull);
    f.cpu.state().gpr[3] = 63;
    f.cpu.execute(r(0x14, 3, 1, 2));
    equal(f.cpu.state().gpr[2], 0);
    f.cpu.execute(r(0x17, 3, 1, 2));
    equal(f.cpu.state().gpr[2], ~0ull);
  }
  {
    Fixture f;
    f.code(0, i(4, 0, 0, 3));
    f.code(4, i(9, 0, 1, 7));
    f.code(16, i(9, 1, 2, 1));
    f.cpu.step();
    equal(f.cpu.state().pc, 0xffffffffa0001004);
    equal(f.cpu.in_delay_slot(), true);
    f.cpu.step();
    equal(f.cpu.state().gpr[1], 7);
    equal(f.cpu.state().pc, 0xffffffffa0001010);
    f.cpu.step();
    equal(f.cpu.state().gpr[2], 8);
  }
  for (unsigned op : {4u, 5u, 0x14u, 0x15u}) {
    Fixture f;
    f.cpu.state().gpr[1] = (op == 4 || op == 0x14) ? 1 : 0;
    f.cpu.execute(i(op, 0, 1, 3));
    equal(f.cpu.state().pc, op & 0x10 ? 0xffffffffa0001008 : 0xffffffffa0001004);
    if (!(op & 0x10)) {
      f.cpu.execute(0x0000000c);
      equal(f.cpu.read_control(Cause) >> 31, 1);
      equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    }
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.execute(r(9, 1, 0, 1));
    equal(f.cpu.state().gpr[1], 0xffffffffa0001008);
    f.cpu.execute(0);
    equal(f.cpu.state().pc, 0xffffffffa0002000);
  }
  {
    Fixture f;
    f.cpu.execute(i(4, 0, 0, 3));
    f.cpu.execute(i(4, 0, 0, 4));
    equal(f.cpu.state().pc, 0xffffffffa0001010);
    f.cpu.execute(0);
    equal(f.cpu.state().pc, 0xffffffffa0001020);
  }
  for (unsigned function : {0x10u, 0x11u, 0x12u, 0x13u}) {
    Fixture f;
    f.cpu.state().gpr[1] = function & 1 ? ~0ull : 0;
    f.cpu.execute(i(1, 1, function, 3));
    equal(f.cpu.state().gpr[31], 0xffffffffa0001008);
    equal(f.cpu.state().pc, function & 2 ? 0xffffffffa0001008 : 0xffffffffa0001004);
  }
  for (bool wide : {false, true}) {
    for (bool is_signed : {false, true}) {
      Fixture f;
      f.cpu.state().gpr[1] = ~0ull;
      f.cpu.state().gpr[2] = 2;
      f.cpu.execute(r((wide ? 0x1c : 0x18) + !is_signed, 1, 2, 0));
      equal(f.cpu.state().lo, wide || is_signed ? ~1ull : 0xfffffffffffffffeull);
      equal(f.cpu.state().hi, wide ? (is_signed ? ~0ull : 1) : (is_signed ? ~0ull : 1));
      equal(f.cpu.state().clocks, wide ? 16 : 10);
    }
  }
  for (bool wide : {false, true}) {
    for (bool is_signed : {false, true}) {
      Fixture f;
      f.cpu.state().gpr[1] = ~2ull;
      f.cpu.state().gpr[2] = 0;
      f.cpu.execute(r((wide ? 0x1e : 0x1a) + !is_signed, 1, 2, 0));
      equal(f.cpu.state().lo, is_signed ? 1 : ~0ull);
      equal(f.cpu.state().hi, ~2ull);
      equal(f.cpu.state().clocks, wide ? 138 : 74);
    }
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0x8000000000000000;
    f.cpu.state().gpr[2] = ~0ull;
    f.cpu.execute(r(0x1e, 1, 2, 0));
    equal(f.cpu.state().lo, 0x8000000000000000);
    equal(f.cpu.state().hi, 0);
    f.cpu.state().gpr[1] = 0xffffffff80000000;
    f.cpu.execute(r(0x1a, 1, 2, 0));
    equal(f.cpu.state().lo, 0xffffffff80000000);
    equal(f.cpu.state().hi, 0);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0x0000000100000002;
    f.cpu.state().gpr[2] = 0x0000000100000003;
    f.cpu.execute(r(0x18, 1, 2, 0));
    equal(f.cpu.state().lo, 6);
    equal(f.cpu.state().hi, 5);
  }
  {
    Fixture f;
    f.cpu.execute(i(4, 0, 0, 3));
    f.cpu.execute(r(0x34, 0, 0, 0));
    equal(f.exception(), 13);
    equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    f.cpu.execute(0x0000000d);
    equal(f.exception(), 9);
    equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    equal(f.cpu.read_control(Cause) >> 31, 1);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x10);
    f.cpu.execute(i(0x19, 0, 1, 1));
    equal(f.exception(), 10);
    equal(f.cpu.state().gpr[1], 0);
  }
}

} // namespace test
