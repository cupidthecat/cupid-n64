#include "core/cartridge/eeprom.hpp"
#include "core/devices/si/serial_interface.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct SerialFixture : MemoryFixture {
  Cic cic;
  Pif pif{cic, ram};
  EventQueue events;
  SerialInterface si{pif, mi, events};
  Eeprom eeprom{events, 512};
  SerialFixture() {
    initialize();
    pif.attach(4, &eeprom);
    mi.lower(Interrupt::Serial);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      if (event == Event::SerialRead)
        si.dma_read();
      if (event == Event::SerialWrite)
        si.dma_write();
      if (event == Event::SerialBusWrite)
        si.complete_write();
      if (event == Event::EepromWrite)
        eeprom.complete_write();
    });
  }
  void boot(bool valid = true) {
    pif.tick();
    equal(static_cast<unsigned>(pif.state()), static_cast<unsigned>(Pif::State::Lockout));
    equal(pif.ram()[0x25], 0xb4);
    equal(pif.ram()[0x26], 0x3f);
    equal(pif.ram()[0x27], 0x3f);
    pif.write_word(0x1fc007fc, 0x10);
    equal(static_cast<unsigned>(pif.state()), static_cast<unsigned>(Pif::State::GetChecksum));
    pif.write_word(0x1fc007f0, 0x0000a536);
    pif.write_word(0x1fc007f4, valid ? 0xc0f1d859 : 0);
    pif.write_word(0x1fc007fc, 0x20);
    equal(pif.ram()[63], 0xa0);
    pif.write_word(0x1fc007fc, 0x40);
    equal(static_cast<unsigned>(pif.state()),
          static_cast<unsigned>(valid ? Pif::State::Terminate : Pif::State::Error));
    if (valid) {
      pif.write_word(0x1fc007fc, 8);
      equal(static_cast<unsigned>(pif.state()), static_cast<unsigned>(Pif::State::Run));
      equal(pif.reset_enabled(), true);
      equal(pif.ram()[63], 0);
    }
  }
};

struct Controller : JoybusDevice {
  unsigned resets = 0;
  void reset() override {
    ++resets;
  }
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override {
    if (input.empty() || input[0] != 1)
      return {};
    if (!output.empty())
      output[0] = 0x80;
    if (output.size() > 1)
      output[1] = 0x10;
    if (output.size() > 2)
      output[2] = 0x20;
    if (output.size() > 3)
      output[3] = 0xe0;
    return {true, output.size() > 4};
  }
};

} // namespace

void pif_tests() {
  {
    SerialFixture f;
    std::array<std::uint8_t, 0x7c0> rom{};
    rom[0] = 0x80;
    rom[3] = 0x12;
    equal(f.pif.load_rom(rom), true);
    equal(f.pif.read_word(0x1fc00000), 0x80000012);
    f.pif.write_word(0x1fc00000, 0xffffffff);
    equal(f.pif.read_word(0x1fc00000), 0x80000012);
    f.pif.power();
    f.cic.power(CicModel::N6102);
    f.boot();
    equal(f.pif.read_word(0x1fc00000), 0);
    f.pif.power();
    equal(f.pif.read_word(0x1fc00000), 0x80000012);
  }
  {
    SerialFixture f;
    unsigned resets = 0;
    f.pif.connect_reset([&] { ++resets; });
    f.boot(false);
    equal(resets, 0);
    f.pif.tick();
    equal(resets, 1);
  }
  {
    SerialFixture f;
    f.si.write(0x1fc007c0, 4, 0x12345678);
    equal(f.si.read_io(24), 0x9b3);
    equal(f.events.time_to_event(), 6450);
    f.si.write(0x1fc007c4, 4, 0xabcdef01);
    equal(f.pif.read_word(0x1fc007c4), 0);
    f.advance(6449);
    equal(f.si.read_io(24), 0x9b3);
    f.advance(1);
    equal(f.si.read_io(24), 0x1000);
    equal(f.mi.read_word(8) & 2, 2);
    f.si.write_io(24, 0);
    equal(f.si.read_io(24), 0);
    equal(f.mi.read_word(8) & 2, 0);
    f.si.write(0x1fc007c0, 4, 0xdeadbeef);
    equal(f.si.read(0x1fc007c4, 4).value, 0xdeadbeef);
    equal(f.si.read_io(24), 0x9b1);
    f.advance(6450);
    equal(f.si.read_io(24), 0x9b1);
  }
  {
    SerialFixture f;
    f.boot();
    Controller controller;
    f.pif.attach(0, &controller);
    f.ram.write(0x1000, 4, 0x01040100);
    f.ram.write(0x1004, 4, 0x000000fe);
    f.ram.write(0x103c, 4, 1);
    f.si.write_io(0, 0x1007);
    f.si.write_io(16, 0x1fc007c1);
    equal(f.si.read_io(0), 0x1000);
    equal(f.si.read_io(16), 0x1fc007c0);
    equal(f.si.read_io(24), 0x411);
    f.advance(12194);
    equal(f.pif.ram()[0], 0);
    f.advance(1);
    equal(f.si.read_io(24), 0x1000);
    equal(f.pif.ram()[0], 1);
    equal(f.pif.ram()[63], 0);
    f.si.write_io(24, 0);
    f.si.write_io(4, 0x1fc007c0);
    equal(f.si.read_io(24), 0x141);
    equal(f.events.time_to_event(), (13600 + 22000 + 1420) * 3);
    f.advance(static_cast<unsigned>(f.events.time_to_event()));
    equal(f.ram.read(0x1000, 8), 0x010401801020e0fe);
    equal(f.si.read_io(24), 0x1000);
  }
  {
    SerialFixture f;
    f.boot();
    Controller controller;
    f.pif.attach(0, &controller);
    f.pif.ram()[0] = 0xfd;
    f.pif.ram()[1] = 0xfe;
    f.pif.write_word(0x1fc007fc, 1);
    f.pif.dma_read(0x1fc007c0, 0x1000);
    equal(controller.resets, 1);
    f.pif.ram()[0] = 1;
    f.pif.ram()[1] = 4;
    f.pif.ram()[2] = 1;
    f.pif.ram()[7] = 0xfe;
    f.pif.write_word(0x1fc007fc, 1);
    f.pif.attach(0, nullptr);
    f.pif.dma_read(0x1fc007c0, 0x1000);
    equal(f.pif.ram()[1], 0x84);
  }
  {
    EventQueue queue;
    Eeprom eeprom(queue, 512);
    std::array<std::uint8_t, 3> status{};
    std::array<std::uint8_t, 1> command{0};
    equal(eeprom.communicate(command, status).valid, true);
    equal(status[1], 0x80);
    equal(status[2], 0);
    std::array<std::uint8_t, 10> write{5, 63, 1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, 1> response{};
    eeprom.communicate(write, response);
    equal(response[0], 0);
    equal(queue.time_to_event(), 1125000);
    eeprom.communicate(command, status);
    equal(status[2], 0x80);
    std::array<std::uint8_t, 2> read{4, 63};
    std::array<std::uint8_t, 8> data{};
    eeprom.communicate(read, data);
    equal(data[0], 255);
    write[2] = 9;
    eeprom.communicate(write, response);
    equal(response[0], 0x80);
    queue.advance(1125000, [&](Event) { eeprom.complete_write(); });
    eeprom.communicate(read, data);
    for (unsigned n = 0; n < data.size(); ++n)
      equal(data[n], n + 1);
    read[1] = 127;
    eeprom.communicate(read, data);
    equal(data[0], 1);
  }
  {
    equal(address_crc(0), 0);
    equal(address_crc(0x8000), 1);
    equal(address_crc(0xc000), 27);
    std::array<std::uint8_t, 32> data{};
    equal(data_crc(data), 0);
    data.fill(255);
    equal(data_crc(data), 10);
  }
}

} // namespace test
