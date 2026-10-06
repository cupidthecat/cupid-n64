#include "../support/test.hpp"
#include <array>
#include <cfenv>

namespace test {
using namespace cupid::n64;
namespace {

constexpr std::uint32_t fp(unsigned function, unsigned fs, unsigned ft, unsigned fd,
                           bool wide = false) {
  return 0x44000000 | ((wide ? 17u : 16u) << 21) | (ft << 16) | (fs << 11) | (fd << 6) | function;
}

constexpr std::uint32_t transfer(unsigned function, unsigned rt, unsigned fs, unsigned cop = 1) {
  return ((0x10 + cop) << 26) | (function << 21) | (rt << 16) | (fs << 11);
}

struct FpuFixture : Fixture {
  FpuFixture() {
    cpu.write_control(Status, 0x34000000);
  }
};

} // namespace

void fpu_tests() {
  {
    Fixture f;
    f.cpu.state().fpr[4] = 0x1234567880000000;
    f.cpu.state().fpr[5] = 0xaaaabbbb11223344;
    f.cpu.execute(transfer(0, 1, 4));
    equal(f.cpu.state().gpr[1], 0xffffffff80000000);
    f.cpu.execute(transfer(0, 1, 5));
    equal(f.cpu.state().gpr[1], 0x12345678);
    f.cpu.state().gpr[1] = 0xdeadbeef;
    f.cpu.execute(transfer(4, 1, 5));
    equal(f.cpu.state().fpr[4], 0xdeadbeef80000000);
    equal(f.cpu.state().fpr[5], 0xaaaabbbb11223344);
    f.cpu.execute(transfer(1, 2, 5));
    equal(f.cpu.state().gpr[2], 0xdeadbeef80000000);
    f.cpu.write_control(Status, 0x34000000);
    f.cpu.execute(transfer(0, 2, 5));
    equal(f.cpu.state().gpr[2], 0x11223344);
    f.cpu.execute(transfer(4, 1, 5));
    equal(f.cpu.state().fpr[5], 0xaaaabbbbdeadbeef);
  }
  for (bool wide : {false, true}) {
    FpuFixture f;
    f.cpu.state().fpr[1] = wide ? 0x3ff8000000000000ull : 0x3fc00000ull;
    f.cpu.state().fpr[2] = wide ? 0x4002000000000000ull : 0x40100000ull;
    f.cpu.state().fpr[3] = ~0ull;
    f.cpu.execute(fp(0, 1, 2, 3, wide));
    equal(f.cpu.state().fpr[3], wide ? 0x400e000000000000ull : 0x40700000ull);
    equal(f.cpu.state().clocks, 6);
    equal(f.cpu.state().fcr31, 0);
  }
  {
    Fixture f;
    f.cpu.state().fpr[4] = 0x3fa00000;
    f.cpu.state().fpr[5] = 0x40200000;
    f.cpu.execute(fp(0, 5, 5, 7));
    equal(f.cpu.state().fpr[7], 0x40700000);
    f.cpu.state().fcr31 = 0x3000;
    f.cpu.state().fpr[4] = 0x000000017f800001;
    f.cpu.execute(fp(6, 5, 0, 7));
    equal(f.cpu.state().fpr[7], 0x000000017f800001);
    equal(f.cpu.state().fcr31, 0x3000);
  }
  for (unsigned rounding = 0; rounding < 4; ++rounding) {
    FpuFixture f;
    f.cpu.state().fcr31 = rounding;
    f.cpu.state().fpr[1] = 0x3f800000;
    f.cpu.state().fpr[2] = 0x33800000;
    f.cpu.execute(fp(0, 1, 2, 3));
    equal(f.cpu.state().fpr[3], rounding == 2 ? 0x3f800001 : 0x3f800000);
    equal(f.cpu.state().fcr31, 0x1004 | rounding);
  }
  {
    FpuFixture f;
    f.cpu.state().fcr31 = 0x80;
    f.cpu.state().fpr[1] = 0x3f800000;
    f.cpu.state().fpr[2] = 0x33800000;
    f.cpu.state().fpr[3] = 0xabcdef;
    f.cpu.execute(i(4, 0, 0, 3));
    f.cpu.execute(fp(0, 1, 2, 3));
    equal(f.exception(), 15);
    equal(f.cpu.state().fcr31, 0x1080);
    equal(f.cpu.state().fpr[3], 0xabcdef);
    equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    equal(f.cpu.read_control(Cause) & 0x80000000, 0x80000000);
    equal(f.cpu.state().clocks, 4);
  }
  for (bool wide : {false, true}) {
    for (bool signaling : {false, true}) {
      FpuFixture f;
      f.cpu.state().fpr[1] = wide ? (signaling ? 0x7ff8000000000001ull : 0x7ff0000000000001ull)
                                  : (signaling ? 0x7fc00001ull : 0x7f800001ull);
      f.cpu.state().fpr[2] = wide ? 0x3ff0000000000000ull : 0x3f800000ull;
      f.cpu.state().fpr[3] = 17;
      f.cpu.execute(fp(0, 1, 2, 3, wide));
      equal(f.exception(), signaling ? 0 : 15);
      equal(f.cpu.state().fcr31, signaling ? 0x10040 : 0x20000);
      equal(f.cpu.state().fpr[3], signaling ? (wide ? 0x7ff7ffffffffffffull : 0x7fbfffffull) : 17);
    }
  }
  for (unsigned rounding = 0; rounding < 4; ++rounding) {
    for (bool negative : {false, true}) {
      FpuFixture f;
      f.cpu.state().fcr31 = 0x01000000 | rounding;
      f.cpu.state().fpr[1] = negative ? 0x80800000 : 0x00800000;
      f.cpu.state().fpr[2] = 0x3f000000;
      f.cpu.execute(fp(2, 1, 2, 3));
      const auto sign = negative ? 0x80000000u : 0;
      const bool minimum = (rounding == 2 && !negative) || (rounding == 3 && negative);
      equal(f.cpu.state().fpr[3], sign | (minimum ? 0x00800000 : 0));
      equal(f.cpu.state().fcr31, 0x0100300c | rounding);
    }
  }
  for (unsigned input : {1u, 0x00800000u}) {
    FpuFixture f;
    f.cpu.state().fpr[1] = input;
    f.cpu.state().fpr[2] = 0x3f000000;
    f.cpu.state().fpr[3] = 44;
    f.cpu.execute(fp(2, 1, 2, 3));
    equal(f.exception(), 15);
    equal(f.cpu.state().fcr31, 0x20000);
    equal(f.cpu.state().fpr[3], 44);
  }
  {
    FpuFixture f;
    f.cpu.state().fpr[1] = 0x3f800000;
    f.cpu.execute(fp(3, 1, 2, 3));
    equal(f.cpu.state().fpr[3], 0x7f800000);
    equal(f.cpu.state().fcr31, 0x8020);
    equal(f.cpu.state().clocks, 58);
  }
  for (bool wide : {false, true}) {
    FpuFixture f;
    f.cpu.state().fpr[1] = wide ? 0xc010000000000000ull : 0xc0800000ull;
    f.cpu.execute(fp(4, 1, 0, 3, wide));
    equal(f.cpu.state().fpr[3], wide ? 0x7ff7ffffffffffffull : 0x7fbfffffull);
    equal(f.cpu.state().fcr31, 0x10040);
    equal(f.cpu.state().clocks, wide ? 116 : 58);
  }
  const std::array<std::uint32_t, 4> integers{2, 2, 3, 2};
  for (unsigned function = 0xc; function <= 0xf; ++function) {
    FpuFixture f;
    f.cpu.state().fpr[1] = 0x40200000;
    f.cpu.execute(fp(function, 1, 0, 3));
    equal(f.cpu.state().fpr[3], integers[function & 3]);
    equal(f.cpu.state().fcr31, 0x1004);
    equal(f.cpu.state().clocks, 10);
  }
  {
    FpuFixture f;
    f.cpu.state().fpr[1] = std::bit_cast<std::uint64_t>(2147483647.5);
    f.cpu.state().fpr[3] = 77;
    f.cpu.execute(fp(0xe, 1, 0, 3, true));
    equal(f.exception(), 15);
    equal(f.cpu.state().fcr31, 0x20000);
    equal(f.cpu.state().fpr[3], 77);
  }
  for (double value : {0x1p53, -0x1p53}) {
    FpuFixture f;
    f.cpu.state().fpr[1] = std::bit_cast<std::uint64_t>(value);
    f.cpu.execute(fp(0x25, 1, 0, 3, true));
    equal(f.exception(), 15);
    equal(f.cpu.state().fcr31, 0x20000);
  }
  for (bool wide : {false, true}) {
    FpuFixture f;
    f.cpu.state().fpr[1] = 0xff80000000000000;
    f.cpu.execute(0x44000000 | (21u << 21) | (1u << 11) | (3u << 6) | (wide ? 0x21 : 0x20));
    equal(f.cpu.state().fpr[3], wide ? 0xc360000000000000ull : 0xdb000000ull);
    equal(f.cpu.state().fcr31, 0);
    equal(f.cpu.state().clocks, 10);
  }
  for (std::uint64_t value : {0x0080000000000000ull, 0xff7fffffffffffffull}) {
    FpuFixture f;
    f.cpu.state().fpr[1] = value;
    f.cpu.state().fpr[3] = 31;
    f.cpu.execute(0x44000000 | (21u << 21) | (1u << 11) | (3u << 6) | 0x21);
    equal(f.exception(), 15);
    equal(f.cpu.state().fpr[3], 31);
    equal(f.cpu.state().fcr31, 0x20000);
  }
  {
    FpuFixture f;
    f.cpu.state().fpr[1] = 0x80000000;
    f.cpu.execute(0x44000000 | (20u << 21) | (1u << 11) | (3u << 6) | 0x20);
    equal(f.cpu.state().fpr[3], 0xcf000000);
    equal(f.cpu.state().fcr31, 0);
    f.cpu.state().fpr[1] = 0xc0200000;
    f.cpu.execute(fp(0xc, 1, 0, 3));
    equal(f.cpu.state().fpr[3], 0xfffffffe);
    equal(f.cpu.state().fcr31, 0x1004);
  }
  for (bool trap : {false, true}) {
    FpuFixture f;
    f.cpu.state().fcr31 = trap ? 0x200 : 0;
    f.cpu.state().fpr[1] = f.cpu.state().fpr[2] = 0x7f7fffff;
    f.cpu.state().fpr[3] = 31;
    f.cpu.execute(fp(0, 1, 2, 3));
    equal(f.exception(), trap ? 15 : 0);
    equal(f.cpu.state().fcr31, trap ? 0x5204 : 0x5014);
    equal(f.cpu.state().fpr[3], trap ? 31 : 0x7f800000);
  }
  {
    FpuFixture f;
    f.cpu.state().fpr[1] = 0x3fc00000;
    f.cpu.execute(fp(0x21, 1, 0, 3));
    equal(f.cpu.state().fpr[3], 0x3ff8000000000000);
    equal(f.cpu.state().clocks, 2);
    f.cpu.execute(fp(0x20, 3, 0, 4, true));
    equal(f.cpu.state().fpr[4], 0x3fc00000);
    equal(f.cpu.state().clocks, 6);
  }
  for (unsigned function = 0x30; function <= 0x3f; ++function) {
    FpuFixture f;
    f.cpu.state().fpr[1] = 0x7f800001;
    f.cpu.state().fpr[2] = 0x3f800000;
    f.cpu.execute(fp(function, 1, 2, 3));
    equal(f.cpu.state().fcr31, (function & 1 ? 0x800000 : 0) | (function & 8 ? 0x10040 : 0));
    equal(f.exception(), 0);
  }
  {
    FpuFixture f;
    f.cpu.state().fcr31 = 0x800800;
    f.cpu.state().fpr[1] = 0x7fc00001;
    f.cpu.state().fpr[2] = 0x3f800000;
    f.cpu.execute(fp(0x32, 1, 2, 3));
    equal(f.exception(), 15);
    equal(f.cpu.state().fcr31, 0x810800);
  }
  for (unsigned condition = 0; condition < 4; ++condition) {
    FpuFixture f;
    f.cpu.state().fcr31 = 0x00830000;
    f.cpu.execute(i(0x11, 8, condition, 3));
    equal(f.cpu.state().pc, condition & 1   ? 0xffffffffa0001004
                            : condition & 2 ? 0xffffffffa0001008
                                            : 0xffffffffa0001004);
    equal(f.cpu.state().fcr31, 0x00800000);
    if (condition & 1) {
      f.cpu.execute(0);
      equal(f.cpu.state().pc, 0xffffffffa0001010);
    }
  }
  {
    FpuFixture f;
    f.cpu.state().gpr[1] = 0x1080;
    f.cpu.execute(transfer(6, 1, 31));
    equal(f.exception(), 15);
    equal(f.cpu.state().fcr31, 0x1080);
    f.cpu.execute(transfer(2, 2, 0));
    equal(f.cpu.state().gpr[2], 0x0a00);
  }
  for (bool wide : {false, true}) {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.state().fpr[4] = 0xaaaaaaaa55555555;
    f.memory.put(0x2000, 8, 0x8000000101234567);
    f.cpu.execute(i(wide ? 0x35 : 0x31, 1, 5, 0));
    equal(f.cpu.state().fpr[4], wide ? 0x8000000101234567 : 0x8000000155555555);
    f.cpu.execute(i(wide ? 0x3d : 0x39, 1, 5, 8));
    equal(f.memory.get(0x2008, wide ? 8 : 4), wide ? 0x8000000101234567 : 0x80000001);
  }
  for (unsigned operation : {0x31u, 0x35u, 0x39u, 0x3du}) {
    for (bool usable : {false, true}) {
      FpuFixture f;
      if (!usable)
        f.cpu.write_control(Status, 0x10000000);
      f.cpu.state().gpr[1] = 0xffffffffa0002001;
      f.cpu.state().fpr[3] = 0x123456789abcdef0;
      f.cpu.state().fcr31 = 0x3000;
      f.cpu.execute(i(operation, 1, 3, 0));
      equal(f.exception(), usable ? (operation & 8 ? 5 : 4) : 11);
      equal(f.memory.transfers.size(), 0);
      equal(f.cpu.state().fpr[3], 0x123456789abcdef0);
      equal(f.cpu.state().fcr31, 0x3000);
    }
  }
  for (unsigned operation : {0x31u, 0x35u}) {
    FpuFixture f;
    f.memory.success = false;
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.state().fpr[3] = 0x123456789abcdef0;
    f.cpu.state().fcr31 = 0x3000;
    f.cpu.execute(i(operation, 1, 3, 0));
    equal(f.exception(), 7);
    equal(f.cpu.state().fpr[3], 0x123456789abcdef0);
    equal(f.cpu.state().fcr31, 0x3000);
  }
  {
    FpuFixture f;
    f.cpu.write_control(Status, 0);
    f.cpu.state().fcr31 = 0x3000;
    f.cpu.state().fpr[3] = 17;
    f.cpu.execute(fp(0, 1, 2, 3));
    equal(f.exception(), 11);
    equal((f.cpu.read_control(Cause) >> 28) & 3, 1);
    equal(f.cpu.state().fcr31, 0x3000);
    equal(f.cpu.state().fpr[3], 17);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x70000000);
    f.cpu.state().gpr[1] = 0x1234567880000001;
    f.cpu.execute(transfer(4, 1, 7, 2));
    f.cpu.execute(transfer(0, 2, 13, 2));
    equal(f.cpu.state().gpr[2], 0xffffffff80000001);
    f.cpu.execute(transfer(1, 2, 19, 2));
    equal(f.cpu.state().gpr[2], 0x1234567880000001);
    f.cpu.execute(transfer(2, 2, 0, 2));
    equal(f.cpu.state().gpr[2], 0xffffffff80000001);
    f.cpu.execute(transfer(3, 2, 0, 2));
    equal(f.exception(), 10);
    equal((f.cpu.read_control(Cause) >> 28) & 3, 2);
  }
  {
    const auto rounding = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    std::feraiseexcept(FE_DIVBYZERO);
    FpuFixture f;
    f.cpu.state().fpr[1] = 0x3f800000;
    f.cpu.state().fpr[2] = 0x33800000;
    f.cpu.state().fcr31 = 2;
    f.cpu.execute(fp(0, 1, 2, 3));
    equal(f.cpu.state().fpr[3], 0x3f800001);
    equal(std::fegetround(), FE_DOWNWARD);
    equal((std::fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0, true);
    std::feclearexcept(FE_ALL_EXCEPT);
    std::fesetround(rounding);
  }
}

} // namespace test
