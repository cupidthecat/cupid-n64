#include "core/cartridge/flash/flash.hpp"
#include "../fixture.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct FlashFixture {
  EventQueue events;
  FlashRam flash;
  explicit FlashFixture(unsigned model) : flash(events, static_cast<FlashModel>(model)) {}
  void select(unsigned offset = 0) {
    equal(flash.select(0x08000000 + offset, {}), true);
  }
  std::uint16_t read() {
    const auto value = flash.read_half({});
    equal(value.has_value(), true);
    return value.value_or(0);
  }
  void command(std::uint32_t command) {
    select(0x10000);
    flash.write_half(static_cast<std::uint16_t>(command >> 16), {});
    flash.write_half(static_cast<std::uint16_t>(command), {});
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      equal(event == Event::FlashComplete, true);
      flash.complete();
    });
  }
};

void identification(unsigned model) {
  FlashFixture f(model);
  const bool macronix = model != 6;
  constexpr unsigned ids[] = {0, 1, 0x1e, 0x1d, 0x84, 0x8e, 0xf1};
  const unsigned id[] = {0x1111, 0x8001, macronix ? 0xc2u : 0x32u, ids[model]};
  equal(f.flash.data().size(), 131072);
  equal(f.flash.select(0x07ffffff, {}), false);
  equal(f.flash.select(0x10000000, {}), false);
  f.command(0xe1000000);
  f.select();
  for (unsigned n = 0; n < 12; ++n)
    equal(f.read(), id[macronix || n < 4 ? n & 3 : 3]);
  f.command(0xd2000000);
  if (macronix) {
    f.select();
    equal(f.read(), 0x1111);
    f.command(0xd2000000);
  }
  f.select();
  if (macronix)
    equal(f.read(), 0x1111);
  equal(f.read(), macronix ? 0x8c : 0x80);
  f.select(0x20000);
  equal(f.flash.read_half({}).has_value(), false);
  f.select();
  equal(f.flash.read_half({}).has_value(), false);
  f.command(0xe1000000);
  f.select(0x20000);
  equal(f.flash.read_half({}).has_value(), macronix);
  f.command(0xf0000000);
  f.select();
  equal(f.read(), 0xffff);
}

void array_reads(unsigned model) {
  FlashFixture f(model);
  const bool words = model < 3;
  const auto mask = words ? 0x3fffu : 0x7fffu;
  f.flash.data()[0] = 0x12;
  f.flash.data()[1] = 0x34;
  f.flash.data()[32764] = 0x56;
  f.flash.data()[32765] = 0x78;
  f.flash.data()[32766] = 0x9a;
  f.flash.data()[32767] = 0xbc;
  f.select(mask - (words ? 1 : 3));
  equal(f.read(), 0x5678);
  equal(f.read(), 0x9abc);
  equal(f.read(), 0x1234);
  f.select(words ? 65536 : 131072);
  equal(f.read(), 0);
}

void programming(unsigned model) {
  FlashFixture f(model);
  const bool macronix = model != 6;
  const unsigned duration = macronix ? 656250 : 56250;
  f.command(0xb4000000);
  f.select();
  for (unsigned n = 0; n < 64; ++n)
    f.flash.write_half(static_cast<std::uint16_t>(0xff00 | n), {});
  f.select();
  f.flash.write_half(0x1234, {});
  f.select();
  equal(f.read(), macronix ? 0x1234 : 0x1200);
  f.command(0xa50003ff);
  equal(f.events.time_to_event(), duration);
  equal(f.flash.data()[131072 - 128], 0x12);
  equal(f.flash.data()[131072 - 127], macronix ? 0x34 : 0);
  f.select();
  if (macronix)
    equal(f.read(), 0x1234);
  equal(f.read(), macronix ? 0x0d : 1);
  f.command(0xe1000000);
  f.select();
  equal(f.read(), macronix ? 0x0d : 1);
  f.advance(duration - 1);
  f.select();
  equal(f.read(), macronix ? 0x0d : 1);
  f.advance(1);
  f.select();
  equal(f.read(), macronix ? 0x8c : 0x84);
  f.flash.write_half(0, {});
  f.select();
  equal(f.read(), macronix ? 0x8c : 0x80);
  f.command(0xb4000000);
  f.select();
  equal(f.read(), 0xffff);
  f.select();
  f.flash.write_half(0x5678, {});
  f.command(0xa50003ff);
  equal(f.flash.data()[131072 - 128], 0x12 & 0x56);
  equal(f.flash.data()[131072 - 127], macronix ? (0x34 & 0x78) : 0);
  f.flash.power();
  f.advance(duration);
  f.command(0xe1000000);
  f.select();
  equal(f.read(), 0x1111);
  FlashFixture restored(model);
  std::copy(f.flash.data().begin(), f.flash.data().end(), restored.flash.data().begin());
  equal(std::equal(f.flash.data().begin(), f.flash.data().end(), restored.flash.data().begin()),
        true);
}

void erasing(unsigned model) {
  FlashFixture f(model);
  const bool macronix = model != 6;
  const unsigned sector_time = macronix ? 15937500 : 52500000;
  const unsigned chip_time = macronix ? 15937500 : 56250000;
  std::fill(f.flash.data().begin(), f.flash.data().end(), 0);
  f.command(0x78000000);
  equal(f.events.time_to_event(), 0x7fffffff);
  for (unsigned sector = 0; sector < 8; ++sector) {
    f.command(0x4b000000 | (sector << 7) | 127);
    f.command(0x78000000);
    equal(f.events.time_to_event(), sector_time);
    for (unsigned n = 0; n < f.flash.data().size(); ++n)
      equal(f.flash.data()[n], n < (sector + 1) * 16384 ? 255 : 0);
    f.advance(sector_time);
    f.select();
    if (macronix)
      f.read();
    equal(f.read(), macronix ? 0x8c : 0x88);
  }
  std::fill(f.flash.data().begin(), f.flash.data().end(), 0);
  f.command(0x3c000000);
  f.command(0x78000000);
  equal(f.events.time_to_event(), chip_time);
  f.advance(chip_time);
  equal(std::all_of(f.flash.data().begin(), f.flash.data().end(),
                    [](auto byte) { return byte == 255; }),
        true);
}

void command_assembly() {
  FlashFixture f(3);
  f.select(0x10000);
  f.flash.write_half(0xe100, {});
  f.select(0x10002);
  f.flash.write_half(0, {});
  f.select();
  equal(f.read(), 0xffff);
  f.command(0xe1000000);
  f.select();
  equal(f.read(), 0x1111);
  f.command(0xd2000000);
  f.command(0xf0000000);
  f.command(0xd2000000);
  f.select();
  equal(f.read(), 0xffff);
  f.command(0xd2000000);
  f.select();
  equal(f.read(), 0xffff);
  equal(f.read(), 0x8c);
}

} // namespace

void flash_tests() {
  EventQueue events;
  FlashRam absent(events);
  equal(absent.data().empty(), true);
  equal(absent.select(0x08000000, {}), false);
  equal(absent.read_half({}).has_value(), false);
  for (unsigned model = 0; model < 7; ++model) {
    identification(model);
    array_reads(model);
    programming(model);
    erasing(model);
  }
  command_assembly();
}

} // namespace test
