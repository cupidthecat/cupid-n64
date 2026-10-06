#include "core/rdp/rdp.hpp"
#include "core/timing/frequencies.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rdp_tests() {
  MemoryFixture f;
  f.initialize();
  Rsp rsp(f.ram, f.mi, f.random);
  Rdp rdp(f.ram, rsp, f.mi);
  equal(rdp.read_word(12), 0x80);
  rdp.write_word(0, 0xff001237);
  rdp.write_word(32, 0x8000);
  equal(rdp.read_word(0), 0x1230);
  equal(rdp.read_word(12), 0x480);
  rdp.write_word(12, 8);
  rdp.write_word(4, 0xaa001257);
  equal(rdp.read_word(4), 0x1250);
  equal(rdp.read_word(8), 0x1230);
  equal(rdp.read_word(12), 0x82);
  rdp.write_word(12, 3 | 16 | 32);
  equal(rdp.read_word(12), 0x87);
  rdp.advance(1);
  equal(rdp.clocks(), clock_frequency - 1);
  equal(rdp.read_word(16), 1);
  rdp.advance(20);
  equal(rdp.read_word(16), 0);
  equal(rdp.read_word(16, 9), 10);
  rdp.advance(clock_frequency);
  equal(rdp.read_word(16), (clock_frequency / 3 + 7) & 0xffffff);
  rdp.write_word(12, 512, 100);
  equal(rdp.read_word(16, 100), 0);
  unsigned requests = 0;
  rdp.connect_sync([&] { ++requests; });
  rdp.read_word(0, 0, false);
  equal(requests, 0);
  rdp.read_word(16, 0, false);
  equal(requests, 1);
  rdp.read_word(0);
  equal(requests, 2);
  rdp.read_word(28);
  equal(requests, 2);
  rdp.connect_sync({});
  rdp.power();
  rdp.write_test(0, 0xffffffff);
  equal(rdp.read_test(0), 3);
  rdp.write_test(4, 0xffffffff);
  equal(rdp.read_test(4), 1);
  for (unsigned n = 0; n < 128; ++n) {
    rdp.write_test(8, n);
    rdp.write_test(12, 0x12345678);
  }
  for (unsigned n = 0; n < 128; ++n) {
    rdp.write_test(8, n + 128);
    equal(rdp.read_test(8), n);
    equal(rdp.read_test(12), n % 4 == 3 ? 0 : n % 4 == 2 ? 0x78 : 0x12345678);
  }
  rdp.write_test(0x20, 255);
  equal(rdp.read_test(0x20), 0);
  equal(rdp.read_test(0x100000), 3);

  std::vector<std::vector<std::uint32_t>> packets;
  unsigned synchronized = 0;
  rdp.connect(
      [&](std::span<const std::uint32_t> words) {
        packets.emplace_back(words.begin(), words.end());
      },
      [&] { ++synchronized; });
  rdp.power();
  f.ram.write(0x1000, 8, 0x0000000000000000);
  f.ram.write(0x1008, 8, 0x0800000012345678);
  for (unsigned n = 0; n < 6; ++n)
    f.ram.write(0x1010 + n * 4, 4, n + 1);
  f.ram.write(0x1028, 8, 0x2900000000000000);
  rdp.write_word(0, 0x1000);
  rdp.write_word(4, 0x1018);
  equal(packets.size(), 0);
  equal(rdp.buffered_words(), 3);
  equal(rdp.consumed_words(), 1);
  equal(rdp.command().start, 0x1018);
  equal(rdp.command().current, 0x1018);
  equal(rdp.read_word(12), 0xa8);
  rdp.write_word(4, 0x1030);
  equal(packets.size(), 2);
  equal(packets[0].size(), 8);
  equal(packets[0][0], 0x08000000);
  equal(packets[0][1], 0x12345678);
  for (unsigned n = 0; n < 6; ++n)
    equal(packets[0][n + 2], n + 1);
  equal(synchronized, 1);
  equal(rdp.buffered_words(), 0);
  equal(rdp.read_word(8), 0x1030);
  equal(rdp.read_word(12), 0x80);
  equal(f.mi.read_word(8) & 32, 32);
  f.mi.write_word(0, 0x800);
  equal(f.mi.read_word(8) & 32, 0);

  constexpr unsigned lengths[] = {4, 6, 12, 14, 12, 14, 20, 22};
  for (unsigned op = 0; op < 8; ++op) {
    rdp.power();
    packets.clear();
    rsp.write_local(0xff8, 4, (op + 8) << 24);
    rsp.write_local(0xffc, 4, 0xabcdef01);
    for (unsigned n = 2; n < lengths[op] * 2; ++n)
      rsp.write_local((0xff8 + n * 4) & 0xfff, 4, n);
    rdp.write_word(12, 2);
    rdp.write_word(0, 0x1ff8);
    rdp.write_word(4, 0x1ff8 + lengths[op] * 8);
    equal(packets.size(), 1);
    equal(packets[0].size(), lengths[op] * 2);
    equal(packets[0][1], 0xabcdef01);
    for (unsigned n = 2; n < lengths[op] * 2; ++n)
      equal(packets[0][n], n);
  }
  rdp.power();
  packets.clear();
  f.ram.write(0x1000, 8, 0x2400000011111111);
  f.ram.write(0x1008, 8, 0x2222222233333333);
  rdp.write_word(12, 8);
  rdp.write_word(0, 0x1000);
  rdp.write_word(4, 0x1010);
  equal(packets.size(), 0);
  rdp.write_word(12, 4 | 8);
  equal(packets.size(), 1);
  equal(packets[0].size(), 4);
  equal(packets[0][2], 0x22222222);
  equal(rdp.command().freeze, true);
  rdp.power();
  rdp.write_word(0, 0);
  rdp.write_word(4, 0x40000);
  equal(rdp.read_word(8), 0);
  equal(rdp.buffered_words(), 0);
  rdp.crash();
  equal(rdp.read_word(12), 0xea);
  rdp.write_word(12, 4 | 64 | 128 | 256);
  equal(rdp.read_word(12), 0xea);
  rdp.sync_full();
  equal(f.mi.read_word(8) & 32, 0);
  equal(rdp.read_word(12), 0xe2);
  rdp.power();
  rdp.connect([&](std::span<const std::uint32_t>) { rdp.crash(); }, {});
  f.ram.write(0x1000, 8, 0x2900000000000000);
  rdp.write_word(0, 0x1000);
  rdp.write_word(4, 0x1008);
  equal(rdp.read_word(12), 0xa2);
  equal(f.mi.read_word(8) & 32, 0);
  rdp.power();
  unsigned completions = 0;
  rdp.connect({}, {}, [&] {
    ++completions;
    rdp.crash();
  });
  rdp.write_word(4, 0);
  equal(completions, 0);
  rdp.write_word(0, 0x1000);
  rdp.write_word(4, 0x1008);
  equal(completions, 1);
  equal(rdp.read_word(12), 0xa2);
}

} // namespace test
