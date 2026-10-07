#include "../fixture.hpp"
#include "core/system/console.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct StorageFixture : MemoryFixture {
  EventQueue events;
  PeripheralInterface pi{ram, mi, events};
  Sram save{98304};
  FlashRam flash{events, FlashModel::Mn63f81mpn};
  explicit StorageFixture(bool use_flash) {
    initialize();
    pi.attach(use_flash ? static_cast<PeripheralDevice &>(flash) : save, 1);
    pi.write_io(44, 15);
  }
  BusRead read(std::uint32_t address, unsigned bytes) override {
    return address >= 0x08000000 ? pi.read(address, bytes) : MemoryFixture::read(address, bytes);
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    return address >= 0x08000000 ? pi.write(address, bytes, value)
                                 : MemoryFixture::write(address, bytes, value);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      if (event == Event::PeripheralBusWrite)
        pi.complete_write();
      if (event == Event::PeripheralRead || event == Event::PeripheralWrite)
        pi.complete_dma();
      if (event == Event::FlashComplete)
        flash.complete();
    });
  }
  void write(std::uint32_t address, std::uint32_t value) {
    pi.write(address, 4, value);
    advance(400);
  }
  void dma(unsigned dram, unsigned bus, bool to_cartridge) {
    pi.write_io(0, dram);
    pi.write_io(4, bus);
    pi.write_io(to_cartridge ? 8 : 12, 127);
    equal(pi.read_io(16) & 1, 1);
    advance(10000);
    equal(pi.read_io(16) & 1, 0);
    equal(pi.read_io(16) & 8, 8);
    pi.write_io(16, 2);
  }
};

void sram_bus() {
  StorageFixture f(false);
  f.cpu.state().gpr[8] = 0xffffffffa8080000;
  f.cpu.state().gpr[9] = 0x89abcdef;
  f.ram.write(0x1000, 4, i(43, 8, 9, 0));
  f.ram.write(0x1004, 4, i(35, 8, 10, 0));
  f.cpu.step();
  equal(f.save.data()[65536], 0x89);
  equal(f.save.data()[65537], 0xab);
  f.advance(400);
  f.cpu.step();
  equal(f.cpu.state().gpr[10], 0xffffffff89abcdefull);
  for (unsigned n = 0; n < 128; ++n)
    f.ram.write(0x2000 + n, 1, n ^ 0xa5);
  f.dma(0x2000, 0x08040000, true);
  for (unsigned n = 0; n < 128; ++n)
    equal(f.save.data()[32768 + n], n ^ 0xa5);
  f.dma(0x3000, 0x08040000, false);
  for (unsigned n = 0; n < 128; ++n)
    equal(f.ram.read(0x3000 + n, 1), n ^ 0xa5);
  equal(f.pi.read(0x08048000, 4).value, 0x80008000);
}

void flash_bus() {
  StorageFixture f(true);
  f.write(0x08010000, 0xe1000000);
  equal(f.pi.read(0x08000000, 4).value, 0x11118001);
  f.dma(0x2000, 0x08000000, false);
  equal(f.ram.read(0x2000, 4), 0x11118001);
  equal(f.ram.read(0x2004, 4), 0x003200f1);
  equal(f.ram.read(0x2008, 4), 0x00f100f1);
  for (unsigned n = 0; n < 128; ++n)
    f.ram.write(0x2000 + n, 1, n ^ 0xa5);
  f.write(0x08010000, 0xb4000000);
  f.dma(0x2000, 0x08000000, true);
  f.write(0x08010000, 0xa5000007);
  equal(f.pi.read(0x08000000, 4).value, 0x00010001);
  f.advance(55849);
  equal(f.pi.read(0x08000000, 4).value, 0x00010001);
  f.advance(1);
  equal(f.pi.read(0x08000000, 4).value, 0x00840084);
  f.write(0x08010000, 0xf0000000);
  f.dma(0x3000, 0x08000380, false);
  for (unsigned n = 0; n < 128; ++n)
    equal(f.ram.read(0x3000 + n, 1), n ^ 0xa5);
}

void console_storage() {
  ConsoleConfig config;
  config.sram_size = 98304;
  auto console = std::make_unique<Console>(config);
  equal(console->sram().data().size(), 98304);
  equal(console->flash().data().empty(), true);
  console->write(0x08080000, 4, 0x12345678);
  console->power();
  equal(console->read(0x08080000, 4).value, 0x12345678);
  config.sram_size = 0;
  config.flash_model = FlashModel::Mn63f81mpn;
  console = std::make_unique<Console>(config);
  auto write = [&](unsigned address, unsigned value) {
    console->write(address, 4, value);
    console->cpu().state().clocks += 400;
    console->synchronize();
  };
  write(0x08010000, 0xb4000000);
  write(0x08000000, 0x12345678);
  write(0x08010000, 0xa5000000);
  equal(console->read(0x08000000, 4).value, 0x00010001);
  console->cpu().state().clocks += 55850;
  console->synchronize();
  equal(console->read(0x08000000, 4).value, 0x00840084);
  console->power();
  equal(console->read(0x08000000, 4).value, 0x12345678);
}

} // namespace

void cartridge_bus_tests() {
  sram_bus();
  flash_bus();
  console_storage();
}

} // namespace test
