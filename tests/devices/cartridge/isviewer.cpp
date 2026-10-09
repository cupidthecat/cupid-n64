#include "../fixture.hpp"
#include "core/system/console.hpp"
#include <limits>

namespace test {
using namespace cupid::n64;
namespace {

void console_bus() {
  std::array<std::uint8_t, 4096> cartridge{};
  cartridge[0] = 0x80;
  cartridge[1] = 0x37;
  cartridge[2] = 0x12;
  cartridge[3] = 0x40;
  std::array<std::uint8_t, 0x7c0> firmware{};
  Console console({.random_seed = 0});
  equal(console.load(cartridge, firmware), true);
  for (unsigned bytes : {1u, 2u, 4u}) {
    const auto value = console.read(0x13ff0020, bytes);
    equal(value.value, 0);
    equal(value.clocks, 540);
  }
  console.write(0x13ff0020, 4, 0x12345678);
  console.write(0x13ff0024, 4, 0x89abcdef);
  equal(console.read(0x04600010, 4).value & 2, 0);
  equal(console.read(0x13ff0020, 4).value, 0x12345678);
  equal(console.read(0x13ff0024, 4).value, 0x89abcdef);
  equal(console.read(0x13ff0021, 1).value & 0xff, 0x34);
  equal(console.read(0x13ff0022, 2).value & 0xffff, 0x89ab);
  console.write(0x13ff0004, 4, 0x12345678);
  console.write(0x13ff0014, 4, 0x13570004);
  equal(console.read(0x13ff0014, 4).value, 0x13570000);
  equal(console.read(0x13ff0004, 4).value, 0);
  equal(console.read(0x13ff0020, 4).value, 0x12345678);
  console.write(0x13fffffc, 4, 0xabcdef01);
  equal(console.read(0x13fffffc, 4).value, 0xabcdef01);
  equal(console.read(0x13ff0000, 4).value, 0);
  console.power(true);
  equal(console.read(0x13ff0020, 4).value, 0);
  equal(console.read(0x13fffffc, 4).value, 0);
  console.write(0x13ff0020, 4, 0x12345678);
  console.power();
  equal(console.read(0x13ff0020, 4).value, 0);
}

void cartridge_mapping() {
  Console console({.random_seed = 0});
  equal(console.read(0x13ff0020, 4).value, 0x00200020);
  std::vector<std::uint8_t> cartridge(0x03ff0004);
  cartridge[0] = 0x80;
  cartridge[1] = 0x37;
  cartridge[2] = 0x12;
  cartridge[3] = 0x40;
  cartridge[0x03fefffc] = 0x12;
  cartridge[0x03fefffd] = 0x34;
  cartridge[0x03fefffe] = 0x56;
  cartridge[0x03feffff] = 0x78;
  cartridge[0x03ff0000] = 0x89;
  cartridge[0x03ff0001] = 0xab;
  cartridge[0x03ff0002] = 0xcd;
  cartridge[0x03ff0003] = 0xef;
  std::array<std::uint8_t, 0x7c0> firmware{};
  equal(console.load(std::span(cartridge).first(0x03ff0000), firmware), true);
  equal(console.read(0x13fefffc, 4).value, 0x12345678);
  equal(console.read(0x13ff0000, 4).value, 0);
  console.write(0x13ff0020, 4, 0x12345678);
  equal(console.load(cartridge, firmware), true);
  equal(console.read(0x13ff0000, 4).value, 0x89abcdef);
  equal(console.read(0x13ff0020, 4).value, 0x00200020);
}

struct ViewerFixture : MemoryFixture {
  EventQueue events;
  PeripheralInterface pi{ram, mi, events};
  IsViewer viewer{pi};
  ViewerFixture() {
    initialize();
    viewer.connect(64);
    pi.attach(viewer, 1);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      if (event == Event::PeripheralBusWrite)
        pi.complete_write();
      if (event == Event::PeripheralRead || event == Event::PeripheralWrite)
        pi.complete_dma();
    });
  }
};

void wrapping_and_completion() {
  ViewerFixture f;
  equal(f.viewer.select(0x13fefffe, {}), false);
  equal(f.viewer.select(0x14000000, {}), false);
  f.pi.write(0x13fffffe, 4, 0x12345678);
  equal(f.pi.read(0x13fffffe, 4).value, 0x12345678);
  equal(f.pi.read(0x13ff0000, 2).value & 0xffff, 0x5678);
  f.viewer.power();
  equal(f.pi.read(0x13fffffe, 4).value, 0);
  for (unsigned n = 0; n < 1024; ++n) {
    f.pi.write(0x13ff0020, 4, n);
    equal(f.pi.read_io(16) & 2, 0);
    equal(f.pi.read_io(52), n * 0x10001u);
    equal(f.events.time_to_event(), 400);
    equal(f.pi.read(0x13ff0020, 4).value, n);
  }
  f.advance(400);
  equal(f.events.time_to_event(), std::numeric_limits<std::int32_t>::max());
  f.pi.write(0x1f000040, 4, 0x89abcdef);
  equal(f.pi.read_io(16) & 2, 2);
  const auto latch = f.pi.read(0x1f000000, 4);
  equal(latch.value, 0x89abcdef);
  equal(latch.clocks, 840);
  equal(f.events.time_to_event(), 400);
  f.advance(400);
  f.viewer.connect(0);
  equal(f.pi.read(0x13ff0020, 4).value, 0x00200020);
}

void dma() {
  ViewerFixture f;
  for (unsigned n = 0; n < 128; ++n)
    f.ram.write(0x2000 + n, 1, n ^ 0xa5);
  f.pi.write_io(0, 0x2000);
  f.pi.write_io(4, 0x13ff0ff0);
  f.pi.write_io(8, 127);
  equal(f.pi.read_io(16) & 3, 1);
  f.advance(100000);
  equal(f.pi.read_io(16) & 9, 8);
  equal(f.events.time_to_event(), std::numeric_limits<std::int32_t>::max());
  for (unsigned n = 0; n < 128; n += 4) {
    const auto value =
        ((n ^ 0xa5) << 24) | (((n + 1) ^ 0xa5) << 16) | (((n + 2) ^ 0xa5) << 8) | ((n + 3) ^ 0xa5);
    equal(f.pi.read(0x13ff0ff0 + n, 4).value, value);
  }
  f.pi.write_io(16, 2);
  f.pi.write_io(0, 0x3000);
  f.pi.write_io(4, 0x13ff0ff0);
  f.pi.write_io(12, 127);
  equal(f.pi.read_io(16) & 3, 1);
  f.advance(100000);
  equal(f.pi.read_io(16) & 9, 8);
  for (unsigned n = 0; n < 128; ++n)
    equal(f.ram.read(0x3000 + n, 1), n ^ 0xa5);
}

} // namespace

void isviewer_tests() {
  console_bus();
  cartridge_mapping();
  wrapping_and_completion();
  dma();
}

} // namespace test
