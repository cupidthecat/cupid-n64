#include "core/rsp/rsp.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_tests() {
  MemoryFixture f;
  f.initialize();
  Rsp rsp(f.ram, f.mi, f.random);
  equal(rsp.read_io(16), 1);
  equal(rsp.read_io(28), 0);
  equal(rsp.read_io(28), 1);
  rsp.write_io(28, 255);
  equal(rsp.read_io(28), 0);
  rsp.write_status(0, 0x81234567);
  equal(rsp.pc(), 0x564);
  equal(rsp.read_status(0), 0x564);
  equal(rsp.read_status(4), 0);
  rsp.write_io(16, 3);
  equal(rsp.status().halted, true);
  rsp.write_io(16, 1 | 16 | 64 | 256 | 1024);
  equal(rsp.read_io(16), 0xe0);
  equal(f.mi.read_word(8) & 1, 1);
  rsp.write_io(16, 24 | 96 | 384 | 1536);
  equal(rsp.read_io(16), 0xe0);
  equal(f.mi.read_word(8) & 1, 1);
  rsp.write_io(16, 2 | 8 | 32 | 128 | 512);
  equal(rsp.read_io(16), 1);
  equal(f.mi.read_word(8) & 1, 0);
  rsp.write_io(16, 0x01555400);
  equal(rsp.read_io(16), 0x7f81);
  rsp.write_io(16, 0x00aaaa00);
  equal(rsp.read_io(16), 1);

  rsp.write(0x04000001, 1, 0xab);
  equal(rsp.read_local(0, 4), 0x00ab0000);
  rsp.write_local(0, 4, 0x11223344);
  rsp.write(0x04000002, 2, 0xcdef);
  equal(rsp.read_local(0, 4), 0x0000cdef);
  rsp.write(0x04000008, 8, 0x123456789abcdef0);
  equal(rsp.read_local(8, 8), 0x1234567800000000);
  equal(rsp.read(0x04000008, 4).clocks, 40);
  rsp.write_local(0x1fff, 8, 0x1020304050607080);
  equal(rsp.read_local(0x1ff8, 8), 0x1020304050607080);
  equal(rsp.read_word(0x04001ffc), 0x50607080);
  equal(rsp.read_word(0x04003ffc), 0x50607080);

  for (unsigned region : {0u, 0x1000u}) {
    rsp.power();
    for (unsigned n = 0; n < 64; ++n)
      f.ram.write(0x1000 + n, 1, n + 1);
    rsp.write_io(0, region | 0xfff);
    rsp.write_io(4, 0x1007);
    rsp.write_io(8, 0x0080100f);
    equal(rsp.read_io(0), region | 0xff8);
    equal(rsp.read_io(4), 0x1000);
    equal(rsp.read_io(8), 0x00801008);
    equal(rsp.read_io(24), 1);
    equal(rsp.dma_clocks(), static_cast<std::uint64_t>(-6));
    rsp.advance_dma(5);
    equal(rsp.read_local(region | 0xff8, 8), 0);
    rsp.advance_dma(1);
    equal(rsp.read_local(region | 0xff8, 8), 0x0102030405060708);
    equal(rsp.read_local(region, 8), 0x090a0b0c0d0e0f10);
    equal(rsp.read_io(0), region | 8);
    equal(rsp.read_io(4), 0x1018);
    equal(rsp.read_io(8), 0x00800008);
    equal(rsp.dma_busy(), true);
    rsp.advance_dma(6);
    equal(rsp.read_local(region | 8, 8), 0x191a1b1c1d1e1f20);
    equal(rsp.read_local(region | 16, 8), 0x2122232425262728);
    equal(rsp.read_io(0), region | 24);
    equal(rsp.read_io(4), 0x1028);
    equal(rsp.read_io(8), 0x00800ff8);
    equal(rsp.dma_busy(), false);
    rsp.write_io(0, region | 0xff8);
    rsp.write_io(4, 0x2000);
    auto *tracker = f.ram.instruction_tracker();
    tracker->watch(0x2000, 16);
    const auto generation = tracker->generation(0x2000);
    rsp.write_io(12, 15);
    rsp.advance_dma(6);
    equal(tracker->generation(0x2000) != generation, true);
    equal(f.ram.read(0x2000, 8), 0x0102030405060708);
    equal(f.ram.read(0x2008, 8), 0x090a0b0c0d0e0f10);
    equal(f.ram.hidden()[0x1000], 0);
    equal(f.ram.hidden()[0x1001], 0);
  }

  rsp.power();
  rsp.write_io(0, 0x100);
  rsp.write_io(4, 0x1000);
  rsp.write_io(8, 0);
  rsp.write_io(0, 0x200);
  rsp.write_io(4, 0x2000);
  rsp.write_io(12, 0);
  equal(rsp.read_io(20), 1);
  equal(rsp.read_io(0), 0x100);
  rsp.write_io(0, 0x300);
  rsp.write_io(4, 0x3000);
  rsp.write_io(8, 0);
  rsp.advance_dma(3);
  equal(rsp.read_io(20), 0);
  equal(rsp.read_io(24), 1);
  equal(rsp.read_io(0), 0x300);
  equal(rsp.read_io(4), 0x3000);
  equal(rsp.read_local(0x100, 8), 0x0102030405060708);
  rsp.advance_dma(3);
  equal(rsp.read_io(24), 0);
  equal(rsp.read_io(0), 0x308);
  equal(rsp.read_io(4), 0x3008);
  equal(rsp.read_local(0x300, 8), 0);

  rsp.power();
  rsp.write_io(0, 0xfff);
  rsp.write_io(4, 0xffffffff);
  rsp.write_io(8, 0x1000, -17);
  equal(rsp.dma_clocks(), static_cast<std::uint64_t>(-20));
  rsp.advance_dma(100);
  equal(rsp.read_io(4), 0);
  equal(rsp.read_io(0), 0);
  equal(rsp.dma_busy(), true);
  equal(rsp.dma_clocks(), static_cast<std::uint64_t>(-3));
  rsp.advance_dma(3);
  equal(rsp.dma_busy(), false);

  unsigned calls = 0;
  unsigned invalid_address = 0;
  unsigned invalid_length = 0;
  rsp.connect_invalidation([&](std::uint32_t address, unsigned length) {
    ++calls;
    invalid_address = address;
    invalid_length = length;
  });
  rsp.write_word(0x04001004, 123);
  equal(calls, 1);
  equal(invalid_address, 4);
  equal(invalid_length, 4);
  rsp.write_io(0, 0x1800);
  rsp.write_io(4, 0x1000);
  rsp.write_io(8, 31);
  rsp.advance_dma(12);
  equal(calls, 2);
  equal(invalid_address, 0x800);
  equal(invalid_length, 32);
  rsp.connect_sync([&] { ++calls; });
  rsp.read_io(16);
  rsp.write_io(16, 0);
  rsp.read_io(28);
  rsp.write_io(28, 1);
  equal(calls, 6);
  rsp.power();
  equal(rsp.read_io(16), 1);
  equal(rsp.pc(), 0);
  equal(rsp.read_local(0x1004, 4), 0);
  equal(rsp.read_local(0, 4), 0);
}

} // namespace test
