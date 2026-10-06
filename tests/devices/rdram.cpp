#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rdram_tests() {
  {
    RamInterface ri;
    RandomGenerator random;
    Rdram ram(ri, random, false);
    equal(ram.size(), 4 * 1024 * 1024);
    equal(ram.hidden().size(), 2 * 1024 * 1024);
    ri.write_word(8, 0);
    ri.write_word(12, 0x14);
    ram.write_word(0x03f80008, 0x00080008, 16);
    ram.write_word(0x03f0000c, 0x02000000);
    ram.write_word(0x03f00004, 0x08000000);
    ram.write_word(0x03f0000c, 0x02000000);
    ram.write_word(0x03f00004, 0x10000000);
    ram.write_word(0x03f00804, 0);
    ram.write_word(0x03f01004, 0x08000000);
    ram.write(0x3ffff8, 8, 0x123456789abcdef0);
    equal(ram.read(0x3ffff8, 8), 0x123456789abcdef0);
    equal(ram.read(0x400000, 8), 0);
    ri.power(true);
    equal(ri.active(), true);
    ram.power(true);
    equal(ram.read(0x3ffff8, 8), 0x123456789abcdef0);
  }
  {
    MemoryFixture f;
    f.ram.write(0, 4, 0x12345678);
    equal(f.ram.read(0, 4), 0);
    equal(f.ri.read_word(24), 1);
    f.ri.write_word(8, 0);
    f.ri.write_word(12, 0x14);
    f.ram.write_word(0x03f0000c, 0x02000000);
    equal(f.ram.read_word(0x03f00000), 0);
    f.mi.write_word(0, 0x10f);
    f.mi.write_rdram(0x03f80008, 4, 0x00080008);
    f.ram.write_word(0x03f0000c, 0x02000000);
    equal(f.ram.read_word(0x03f00000), 0xb4190010);
    equal(f.ram.read_word(0x03f00008), 0x030b020b);
    equal(f.ram.read_word(0x03f00024), 0x500);
    equal(f.ram.read_word(0x03f80000), 0);
    f.ram.write_word(0x03f80200, 0xabcdef);
    equal(f.ram.read_word(0x03f00200), 0xabcdef);
  }
  {
    MemoryFixture f;
    f.initialize();
    for (unsigned chip = 0; chip < 4; ++chip) {
      f.ram.write(chip * 0x200000, 4, 0x80000001 + chip);
      equal(f.ram.read(chip * 0x200000, 4), 0x80000001 + chip);
      equal(f.ram.read_word(0x03f00004 + chip * 0x800), chip * 2 << 26);
    }
    f.ram.write(0x800000, 4, 0x1234);
    equal(f.ram.read(0x800000, 4), 0);
    f.ram.write(0x1000, 8, 0x0123456789abcdef);
    for (unsigned offset = 0; offset < 8; ++offset)
      equal(f.ram.read(0x1000 + offset, 1), (0x0123456789abcdefull >> ((7 - offset) * 8)) & 255);
    f.ram.write(0x1001, 1, 0xff);
    f.ram.write(0x1006, 2, 0x1234);
    equal(f.ram.read(0x1000, 8), 0x01ff456789ab1234);
    f.ram.power(true);
    equal(f.ram.read(0x1000, 8), 0x01ff456789ab1234);
    f.ram.power();
    equal(f.ram.read(0x1000, 8), 0);
  }
  {
    MemoryFixture f;
    f.initialize();
    f.ram.write_word(0x03f0080c, 0x02c0c0c0);
    f.ram.write(0x200000, 8, 0xffffffffffffffff);
    equal(f.ram.read(0x200000, 8), 0);
    f.ram.write_word(0x03f0080c, 0x02000000);
    equal(f.ram.read(0x200000, 8), 0xffffffffffffffff);
    f.ram.write_word(0x03f00804, 0x28000000);
    equal(f.ram.read(0x200000, 8), 0);
    equal(f.ram.read(0xa00000, 8), 0xffffffffffffffff);
  }
  {
    MemoryFixture f;
    f.initialize();
    f.ram.write(0x1000, 8, 0x0001000000010000);
    equal(f.ram.read(0x1000, 8, true), 0x0000000c0000000c);
    f.ram.write(0x1001, 1, 1);
    equal(f.ram.hidden()[0x800], 3);
    f.ram.write(0x1000, 1, 1);
    equal(f.ram.hidden()[0x800], 1);
    f.ram.write(0x1000, 8, 0x0000000a00000005, true);
    equal(f.ram.read(0x1000, 8, true), 0x0000000a00000005);
    std::array<std::uint32_t, 4> values{1, 0x10000, 0x10001, 0};
    f.ram.write_burst(0x2000, values);
    equal(f.ram.read(0x2000, 8, true), 0x000000030000000c);
    equal(f.ram.read(0x2008, 8, true), 0x0000000f00000000);
    std::array<std::uint32_t, 4> result{};
    f.ram.read_burst(0x2000, result);
    for (unsigned n = 0; n < values.size(); ++n)
      equal(result[n], values[n]);
    equal(reinterpret_cast<std::uintptr_t>(f.ram.words().data()) % 65536, 0);
    std::vector<std::uint8_t> coverage(f.ram.size() / 2);
    equal(f.ram.bind_hidden(std::span(coverage).first(8)), false);
    equal(f.ram.bind_hidden(coverage), true);
    equal(coverage[0x1001], 3);
    f.ram.write(0x2000, 4, 0x00010001);
    equal(coverage[0x1000], 3);
    equal(coverage[0x1001], 3);
    coverage[0x1000] = 1;
    equal(f.ram.bind_hidden({}), true);
    equal(f.ram.hidden()[0x1000], 1);
    equal(f.ram.hidden().data() == coverage.data(), false);
  }
}

} // namespace test
