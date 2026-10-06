#include "core/devices/pi/peripheral_interface.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct CartridgeMemory : PeripheralMemory {
  std::array<std::uint8_t, 4096> data{};
  bool select(std::uint32_t address, PeripheralTiming timing) override {
    if (address < 0x10000000 || address >= 0x10000000 + data.size() || !timing.permits(minimum_))
      return false;
    view_ = data;
    offset_ = address - 0x10000000;
    writable_ = true;
    return true;
  }
};

struct PeripheralFixture : MemoryFixture {
  EventQueue events;
  PeripheralInterface pi{ram, mi, events};
  CartridgeMemory cartridge;
  PeripheralFixture() {
    initialize();
    pi.attach(cartridge, 0);
    mi.lower(Interrupt::Peripheral);
    for (unsigned n = 0; n < cartridge.data.size(); ++n)
      cartridge.data[n] = static_cast<std::uint8_t>(n);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      if (event == Event::PeripheralRead || event == Event::PeripheralWrite)
        pi.complete_dma();
      if (event == Event::PeripheralBusWrite)
        pi.complete_write();
    });
  }
};

} // namespace

void pi_tests() {
  {
    PeripheralFixture f;
    equal(f.pi.read(0x10000000, 4).value, 0x00010203);
    equal(f.pi.read(0x10000000, 4).clocks, 540);
    equal(f.pi.read_io(4), 0x10000004);
    equal(f.pi.read(0x1ffffffc, 4).value, 0xfffcfffc);
    equal(f.pi.read(0x10000ffe, 4).value, 0xfefffeff);
    f.pi.detach(f.cartridge);
    equal(f.pi.read(0x10000010, 4).value, 0x00100010);
    f.pi.attach(f.cartridge, 0);
    f.pi.write_io(0, 0x12345679);
    equal(f.pi.read_io(0), 0x00345678);
    f.pi.write_io(4, 0x12345679);
    equal(f.pi.read_io(4), 0x12345678);
    for (unsigned offset : {20u, 24u, 28u, 32u, 36u, 40u, 44u, 48u}) {
      f.pi.write_io(offset, 0xffffffff);
      equal(f.pi.read_io(offset), (offset & 15) == 12 ? 15 : (offset & 15) == 0 ? 3 : 255);
    }
  }
  {
    PeripheralFixture f;
    f.pi.write(0x10000000, 4, 0xabcdef01);
    equal(f.pi.read_io(16), 2);
    equal(f.events.time_to_event(), 400);
    f.pi.write(0x10000004, 4, 0x12345678);
    equal(f.cartridge.data[4], 4);
    f.pi.write_io(0, 0x1234);
    equal(f.pi.read_io(0), 0);
    equal(f.pi.read_io(16), 6);
    f.advance(100);
    const auto value = f.pi.read(0x10000004, 4);
    equal(value.value, 0xabcdef01);
    equal(value.clocks, 640);
    equal(f.pi.read_io(16), 4);
    f.advance(300);
    equal(f.pi.read(0x10000004, 4).value, 0x04050607);
    f.pi.write_io(16, 1);
    equal(f.pi.read_io(16), 0);
  }
  {
    PeripheralFixture f;
    f.pi.write(0x10000000, 1, 0xff);
    f.advance(400);
    equal(f.pi.read(0x10000000, 4).value, 0xff000000);
    f.pi.write(0x10000006, 2, 0x1234);
    f.advance(400);
    equal(f.pi.read(0x10000004, 4).value, 0x04050000);
    equal(f.pi.read(0x10000008, 4).value, 0x12340a0b);
  }
  {
    PeripheralFixture f;
    f.pi.write_io(0, 0x1000);
    f.pi.write_io(4, 0x10000000);
    f.pi.write_io(12, 15);
    equal(f.pi.read_io(0), 0x1010);
    equal(f.pi.read_io(4), 0x10000010);
    equal(f.pi.read_io(12), 127);
    equal(f.pi.read_io(16), 1);
    equal(f.ram.read(0x1000, 8), 0x0001020304050607);
    equal(f.ram.read(0x1008, 8), 0x08090a0b0c0d0e0f);
    equal(f.events.time_to_event(), 396);
    f.advance(395);
    equal(f.pi.read_io(16), 1);
    f.advance(1);
    equal(f.pi.read_io(16), 8);
    equal(f.mi.read_word(8) & 16, 16);
    f.pi.write_io(16, 2);
    equal(f.pi.read_io(16), 0);
    equal(f.mi.read_word(8) & 16, 0);
  }
  {
    PeripheralFixture f;
    f.pi.write_io(0, 0x1002);
    f.pi.write_io(4, 0x10000000);
    f.pi.write_io(12, 7);
    equal(f.ram.read(0x1000, 8), 0x0000000102030405);
    equal(f.pi.read_io(0), 0x1008);
    equal(f.pi.read_io(4), 0x10000008);
    equal(f.pi.read_io(12), 125);
    f.pi.write_io(16, 1);
    f.advance(10000);
    equal(f.pi.read_io(16), 0);
    equal(f.mi.read_word(8) & 16, 0);
  }
  {
    PeripheralFixture f;
    f.ram.write(0x1000, 8, 0x123456789abcdef0);
    f.pi.write_io(0, 0x1000);
    f.pi.write_io(4, 0x10000000);
    f.pi.write_io(8, 6);
    equal(f.pi.read_io(8), 8);
    equal(f.pi.read_io(0), 0x1000);
    equal(f.pi.read_io(4), 0x10000000);
    f.advance(10000);
    equal(f.pi.read(0x10000000, 4).value, 0x12345678);
    equal(f.pi.read(0x10000004, 4).value, 0x9abcdef0);
  }
}

} // namespace test
