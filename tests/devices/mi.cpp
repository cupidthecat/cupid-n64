#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void mi_tests() {
  {
    MemoryFixture f;
    equal(f.mi.read_word(4), 0x02020102);
    equal(f.mi.read_word(8), 63);
    equal(f.mi.read_word(12), 0);
    f.mi.write_word(12, 0xfff);
    equal(f.mi.read_word(12), 63);
    equal(f.cpu.read_control(Cause) & 0x400, 0x400);
    for (unsigned source = 0; source < 6; ++source)
      f.mi.lower(static_cast<Interrupt>(source));
    equal(f.cpu.read_control(Cause) & 0x400, 0);
    for (unsigned source = 0; source < 6; ++source) {
      f.mi.raise(static_cast<Interrupt>(source));
      equal(f.mi.read_word(8), 1u << source);
      equal(f.cpu.read_control(Cause) & 0x400, 0x400);
      f.mi.write_word(12, 1u << (source * 2));
      equal(f.cpu.read_control(Cause) & 0x400, 0);
      f.mi.write_word(12, 2u << (source * 2));
      equal(f.cpu.read_control(Cause) & 0x400, 0x400);
      f.mi.lower(static_cast<Interrupt>(source));
    }
    f.mi.raise(Interrupt::Display);
    f.mi.write_word(0, 0x800);
    equal(f.mi.read_word(8), 0);
    equal(f.cpu.read_control(Cause) & 0x400, 0);
    f.mi.write_word(0, 0x357f);
    equal(f.mi.read_word(0), 0x3ff);
    f.mi.write_word(0, 0x1280);
    equal(f.mi.read_word(0), 0);
  }
  {
    MemoryFixture f;
    f.mi.write_word(12, 0x80);
    f.cpu.write_control(Status, 0x30000401);
    f.cpu.execute(0);
    equal((f.cpu.read_control(Cause) >> 2) & 31, 0);
    equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    equal(f.cpu.state().pc, 0xffffffff80000180);
    equal(f.cpu.state().clocks, 2);
  }
  {
    MemoryFixture f;
    f.initialize();
    f.mi.write_word(0, 0x10f);
    f.mi.write_rdram(0x1000, 4, 0x12345678);
    equal(f.ram.read(0x1000, 8), 0x1234567812345678);
    equal(f.ram.read(0x1008, 8), 0x1234567812345678);
    equal(f.ram.read(0x1010, 8), 0);
    equal(f.mi.read_word(0), 15);
    f.mi.write_rdram(0x1020, 4, 0xabcdef01);
    equal(f.ram.read(0x1020, 8), 0xabcdef0100000000);
    f.mi.write_word(0, 0x11f);
    f.mi.write_rdram(0x17f8, 8, 0x8000000180000003);
    equal(f.ram.read(0x17f8, 8), 0x8000000180000003);
    equal(f.ram.read(0x1000, 8), 0x8000000180000003);
    equal(f.ram.read(0x1010, 8), 0x8000000180000003);
  }
  {
    MemoryFixture f;
    f.initialize();
    equal(f.mi.read_rdram(0x03f00000, 4).value, 0xb4190010);
    equal(f.mi.read_rdram(0x03f00000, 4).clocks, 40);
    equal(f.mi.read_rdram(0x03f00004, 4).value, 0);
    f.mi.write_word(0, 0x2000);
    equal(f.mi.read_rdram(0x03f00004, 4).value, 0);
    equal(f.mi.read_rdram(0x03f00804, 4).value, 0x08000000);
    std::array<std::uint32_t, 4> words{1, 2, 3, 4};
    f.mi.read_burst(0x03f00000, words);
    equal(words[0], 0xb4190010);
    equal(words[1] | words[2] | words[3], 0);
    f.mi.write_word(0, 0x400);
    f.mi.read_burst(0, words);
    equal(f.mi.frozen(), true);
    equal(words[0], 0xb4190010);
  }
}

} // namespace test
