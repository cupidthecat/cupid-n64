#include "../fixture.hpp"
#include "core/cartridge/joybus.hpp"
#include "core/devices/pif/pif.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct CartridgePacket : MemoryFixture {
  EventQueue events;
  Cic cic;
  Pif pif{cic, ram};
  Eeprom eeprom;
  Rtc rtc;
  CartridgeJoybus cartridge{eeprom, rtc};
  explicit CartridgePacket(unsigned size, bool clock)
      : eeprom(events, size), rtc(events, clock, [] { return 1700000000; }) {
    initialize();
    pif.attach(4, &cartridge);
    boot();
  }
  void boot() {
    cic.power(CicModel::N6102);
    pif.tick();
    pif.write_word(0x1fc007fc, 0x10);
    pif.write_word(0x1fc007f0, 0x0000a536);
    pif.write_word(0x1fc007f4, 0xc0f1d859);
    pif.write_word(0x1fc007fc, 0x20);
    pif.write_word(0x1fc007fc, 0x40);
    pif.write_word(0x1fc007fc, 8);
    equal(static_cast<unsigned>(pif.state()), static_cast<unsigned>(Pif::State::Run));
  }
  void write() {
    std::array<std::uint8_t, 3> input{5, 3, 0x69};
    std::array<std::uint8_t, 1> output{};
    equal(cartridge.communicate(input, output).valid, true);
    equal(output[0], 0);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      if (event == Event::EepromWrite)
        eeprom.complete_write();
      else if (event == Event::RtcTick)
        rtc.tick();
      else
        equal(false, true);
    });
  }
  void packet(unsigned send, unsigned receive, unsigned padding, unsigned size, bool busy,
              unsigned receive_flags) {
    std::fill(pif.ram().begin(), pif.ram().end(), 0xa5);
    for (unsigned n = 0; n < 4; ++n)
      pif.ram()[n] = 0;
    for (unsigned n = 4; n < 4 + padding; ++n)
      pif.ram()[n] = 255;
    const unsigned start = 4 + padding;
    pif.ram()[start] = 0x40;
    pif.ram()[start + 1] = static_cast<std::uint8_t>(receive | receive_flags);
    pif.ram()[start + 2 + receive] = 0xfe;
    const auto command = (unsigned(pif.ram()[60]) << 24) | (unsigned(pif.ram()[61]) << 16) |
                         (unsigned(pif.ram()[62]) << 8) | 1u;
    pif.write_word(0x1fc007fc, command);
    pif.ram()[start] = static_cast<std::uint8_t>(send);
    pif.dma_read(0x1fc007c0, 0);
    const bool skipped = (send & 0xc0) != 0;
    equal(pif.ram()[start], send);
    equal(pif.ram()[start + 1], skipped ? receive | receive_flags : receive | (size ? 0 : 0x80));
    const std::array<unsigned, 3> expected{0, size == 512 ? 0x80u : 0xc0u, busy ? 0x80u : 0u};
    for (unsigned n = 0; n < receive; ++n)
      equal(pif.ram()[start + 2 + n], size && !skipped ? expected[n] : 0xa5);
    equal(pif.ram()[start + 2 + receive], 0xfe);
    equal(pif.ram()[63], 0);
  }
  void skipped_packet(unsigned padding) {
    std::fill(pif.ram().begin(), pif.ram().end(), 0);
    for (unsigned n = 4; n < 4 + padding; ++n)
      pif.ram()[n] = 255;
    const unsigned start = 4 + padding;
    pif.ram()[start + 1] = 3;
    pif.ram()[start + 2] = 0xa5;
    pif.ram()[start + 3] = 0xa5;
    pif.ram()[start + 4] = 0xa5;
    pif.ram()[start + 5] = 0xfe;
    const auto command = (unsigned(pif.ram()[60]) << 24) | (unsigned(pif.ram()[61]) << 16) |
                         (unsigned(pif.ram()[62]) << 8) | 1u;
    pif.write_word(0x1fc007fc, command);
    pif.dma_read(0x1fc007c0, 0);
    equal(pif.ram()[start + 1], 3);
    for (unsigned n = 0; n < 3; ++n)
      equal(pif.ram()[start + 2 + n], 0xa5);
  }
};

} // namespace

void cartridge_status_tests() {
  for (unsigned size : {0u, 512u, 2048u})
    for (bool clock : {false, true})
      for (unsigned send : {0u, 0x40u, 0x80u, 0xc0u})
        for (unsigned receive : {0u, 1u, 2u, 3u})
          for (unsigned padding : {0u, 7u, 50u})
            for (unsigned receive_flags : {0u, 0xc0u}) {
              CartridgePacket fixture(size, clock);
              std::array<std::uint8_t, 3> direct{0xa5, 0xa5, 0xa5};
              equal(fixture.cartridge.communicate({}, direct).valid, false);
              for (auto byte : direct)
                equal(byte, 0xa5);
              fixture.skipped_packet(padding);
              fixture.packet(send, receive, padding, size, false, receive_flags);
              if (size) {
                fixture.write();
                fixture.packet(send, receive, padding, size, true, receive_flags);
                fixture.advance(1124999);
                fixture.packet(send, receive, padding, size, true, receive_flags);
                fixture.advance(1);
                fixture.packet(send, receive, padding, size, false, receive_flags);
                fixture.write();
                fixture.events.reset();
                fixture.pif.power();
                fixture.boot();
                fixture.rtc.power();
                fixture.advance(2250000);
                fixture.packet(send, receive, padding, size, true, receive_flags);
                equal(fixture.eeprom.data()[24], 0x69);
              }
            }
}

} // namespace test
