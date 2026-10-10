#include "../../system/reset/fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
using namespace cupid::n64;
namespace {
struct BusProbe : reset_fixture::Machine {
  unsigned device = 0;
  unsigned channel = 0;
  EventQueue queue;
  std::unique_ptr<SerialInterface> serial;
  unsigned read(unsigned address) {
    return address >= 0x04800000 && address < 0x04900000 ? serial->read_io(address)
                                                         : reset_fixture::Machine::read(address);
  }
  void write(unsigned address, unsigned value) {
    if (address >= 0x04800000 && address < 0x04900000)
      serial->write_io(address, value);
    else
      reset_fixture::Machine::write(address, value);
  }
  void connect(unsigned choice) {
    device = choice;
    if (choice == 4) {
      console->connect_mouse(channel);
      console->mouse(channel).input(false, false, 0, 0);
    } else {
      console->connect_controller(channel, true);
      auto &pad = console->controller(channel);
      pad.input(0, 0, 0);
      if (choice == 1) {
        pad.memory_pak(1);
        for (unsigned n = 0; n < pad.pak_data().size(); ++n)
          pad.pak_data()[n] = peripheral_bus::pattern(n);
      }
      if (choice == 2)
        pad.rumble_pak();
      if (choice == 3)
        pad.transfer_pak();
    }
  }
  void disconnect() {
    if (device == 4)
      console->connect_mouse(channel, false);
    else
      console->controller(channel).disconnect_pak();
  }
  void boot() {
    auto &pif = console->pif();
    pif.tick();
    pif.write_word(0x1fc007fc, 0x10);
    pif.write_word(0x1fc007f0, 0x0000a536);
    pif.write_word(0x1fc007f4, 0xc0f1d859);
    pif.write_word(0x1fc007fc, 0x20);
    pif.write_word(0x1fc007fc, 0x40);
    pif.write_word(0x1fc007fc, 8);
    if (pif.state() != Pif::State::Run)
      throw std::runtime_error("Peripheral bus boot did not reach Run");
  }
  void set_epoch(unsigned epoch) {
    queue.reset();
    queue.advance(epoch, [](Event) {});
    serial = std::make_unique<SerialInterface>(console->pif(), console->interrupts(), queue);
    for (unsigned n = 0; n < 64; ++n)
      console->ram().write(peripheral_bus::ReplyAddress + n, 1, 0x6d);
  }
  void packet(const std::array<std::uint8_t, 64> &data) {
    for (unsigned n = 0; n < data.size(); ++n)
      console->ram().write(peripheral_bus::PacketAddress + n, 1, data[n]);
  }
  unsigned timing() {
    return console->pif().estimate_timing();
  }
  void advance(unsigned clocks) {
    queue.advance(clocks, [&](Event event) {
      if (event == Event::SerialRead)
        serial->dma_read();
      else if (event == Event::SerialWrite)
        serial->dma_write();
      else
        throw std::runtime_error("Unexpected peripheral bus event");
    });
  }
  unsigned address_crc(unsigned address) {
    return cupid::n64::address_crc(address);
  }
  bool motor() {
    return console->controller(channel).rumbling();
  }
  void finish() {
    reset_fixture::Machine::finish(peripheral_bus::expected, "Peripheral bus commands");
  }
};
} // namespace
void peripheral_bus_tests() {
  BusProbe probe;
  peripheral_bus::scenarios(probe);
  equal(probe.block, peripheral_bus::expected.size());
  equal(probe.observations, 486720);
}
} // namespace test
