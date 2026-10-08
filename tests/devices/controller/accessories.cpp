#include "../fixture.hpp"
#include "core/controller/accessories/cartridge/handheld.hpp"
#include "core/controller/gamepad.hpp"
#include "core/controller/mouse/mouse.hpp"
#include "core/system/console.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct Cartridge : TransferCartridge {
  std::array<std::uint8_t, 0x10000> memory{};
  unsigned resets = 0;
  bool present() const override {
    return true;
  }
  void power() override {
    ++resets;
  }
  std::uint8_t read(std::uint16_t address) override {
    return memory[address];
  }
  void write(std::uint16_t address, std::uint8_t value) override {
    memory[address] = value;
  }
};

struct PakFixture {
  RandomGenerator random;
  Gamepad pad{random};
  std::array<std::uint8_t, 33> read(unsigned address, unsigned size = 33,
                                    bool valid_address = true) {
    const auto crc = address_crc(static_cast<std::uint16_t>(address));
    std::array<std::uint8_t, 3> command{
        2, static_cast<std::uint8_t>(address >> 8),
        static_cast<std::uint8_t>((address & 0xe0) | (crc ^ unsigned(!valid_address)))};
    std::array<std::uint8_t, 33> response;
    response.fill(0xa5);
    const auto status = pad.communicate(command, std::span(response).first(size));
    equal(status.valid, size != 0);
    equal(status.overflow, false);
    return response;
  }
  void write(unsigned address, std::uint8_t value) {
    std::array<std::uint8_t, 35> command{
        3, static_cast<std::uint8_t>(address >> 8),
        static_cast<std::uint8_t>((address & 0xe0) |
                                  address_crc(static_cast<std::uint16_t>(address)))};
    std::fill(command.begin() + 3, command.end(), value);
    std::array<std::uint8_t, 4> response{0xa5, 0xa5, 0xa5, 0xa5};
    equal(pad.communicate(command, response).valid, true);
    equal(response[0], data_crc(std::span<const std::uint8_t, 32>(command.data() + 3, 32)));
    equal(response[1], 0xa5);
  }
  unsigned identify() {
    std::array<std::uint8_t, 1> command{0};
    std::array<std::uint8_t, 5> response{0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
    equal(pad.communicate(command, response).valid, true);
    equal(response[0], 5);
    equal(response[1], 0);
    equal(response[3], 0xa5);
    return response[2];
  }
};

void boot(Pif &pif) {
  pif.tick();
  pif.write_word(0x1fc007fc, 0x10);
  pif.write_word(0x1fc007f0, 0x0000a536);
  pif.write_word(0x1fc007f4, 0xc0f1d859);
  pif.write_word(0x1fc007fc, 0x20);
  pif.write_word(0x1fc007fc, 0x40);
  pif.write_word(0x1fc007fc, 8);
}

void poll(Console &console, unsigned port, std::span<const std::uint8_t> command,
          unsigned receive) {
  auto ram = console.pif().ram();
  std::fill(ram.begin(), ram.end(), 0);
  ram[port] = static_cast<std::uint8_t>(command.size());
  ram[port + 1] = static_cast<std::uint8_t>(receive);
  std::copy(command.begin(), command.end(), ram.begin() + port + 2);
  ram[port + 2 + command.size() + receive] = 0xfe;
  console.pif().write_word(0x1fc007fc, 1);
  console.pif().dma_read(0x1fc007c0, 0x1000);
}

} // namespace

void accessory_tests() {
  {
    Mouse mouse;
    mouse.input(true, true, 800, -800);
    equal(mouse.state(), 0xc0007f7f);
    mouse.input(false, true, -800, 800);
    equal(mouse.state(), 0x40008080);
    mouse.input(true, false, 18, -27);
    equal(mouse.state(), 0x8000121b);
    for (unsigned receive = 0; receive <= 70; ++receive) {
      std::array<std::uint8_t, 70> response;
      response.fill(0xa5);
      std::array<std::uint8_t, 1> command{0};
      equal(mouse.communicate(command, std::span(response).first(receive)).valid, true);
      for (unsigned byte = 0; byte < response.size(); ++byte)
        equal(response[byte], byte < std::min(receive, 3u) ? (byte == 0   ? 2
                                                              : byte == 1 ? 0
                                                                          : 2)
                                                           : 0xa5);
      response.fill(0xa5);
      command[0] = 1;
      const auto status = mouse.communicate(command, std::span(response).first(receive));
      equal(status.valid, true);
      equal(status.overflow, receive > 4);
      constexpr std::array<std::uint8_t, 4> state{0x80, 0, 18, 27};
      for (unsigned byte = 0; byte < response.size(); ++byte)
        equal(response[byte], byte < std::min(receive, 4u) ? state[byte] : 0xa5);
    }
    std::array<std::uint8_t, 1> command{2};
    std::array<std::uint8_t, 5> response{1, 2, 3, 4, 5};
    equal(mouse.communicate(command, response).valid, false);
    equal(response[4], 5);
  }
  {
    PakFixture f;
    auto cartridge = std::make_shared<Cartridge>();
    for (unsigned address = 0; address < cartridge->memory.size(); ++address)
      cartridge->memory[address] = static_cast<std::uint8_t>((address >> 8) ^ address);
    f.pad.transfer_pak(cartridge);
    equal(f.read(0x8000)[32], 255);
    equal(f.identify(), 3);
    equal(f.identify(), 1);
    equal(f.read(0x8000)[0], 0);
    f.write(0x8000, 0x84);
    equal(f.read(0x8000)[0], 0x84);
    equal(f.read(0xa000)[0], 3);
    equal(f.read(0xb000)[0], 0x80);
    f.write(0xb000, 1);
    equal(cartridge->resets, 1);
    auto response = f.read(0xb000);
    equal(response[0], 0x8d);
    equal(response[1], 0x89);
    equal(response[32], data_crc(std::span<const std::uint8_t, 32>(response.data(), 32)));
    f.write(0xb000, 1);
    equal(cartridge->resets, 1);
    f.write(0xb000, 0);
    response = f.read(0xb000);
    equal(response[0], 0x88);
    equal(response[1], 0x84);
    equal(response[2], 0x80);
    f.write(0xb000, 1);
    equal(cartridge->resets, 2);
    for (unsigned bank = 0; bank < 4; ++bank) {
      f.write(0xa000, static_cast<std::uint8_t>(bank));
      for (unsigned address = 0; address < 0x4000; address += 32) {
        response = f.read(0xc000 + address);
        const unsigned mapped = bank * 0x4000 + address;
        const bool accessible = mapped <= 0x7fff || (mapped >= 0xa000 && mapped <= 0xbfff);
        for (unsigned byte = 0; byte < 32; ++byte)
          equal(response[byte], accessible ? cartridge->memory[mapped + byte] : 0);
        equal(response[32], data_crc(std::span<const std::uint8_t, 32>(response.data(), 32)));
      }
    }
    f.write(0xa000, 2);
    f.write(0xe000, 0x6a);
    for (unsigned byte = 0; byte < 32; ++byte)
      equal(cartridge->memory[0xa000 + byte], 0x6a);
    equal(f.read(0xe000, 33, false)[32], 255);
    f.write(0xa000, 4);
    equal(f.read(0xa000)[0], 0);
    f.write(0x8000, 0xfe);
    equal(f.read(0xc000)[0], 0);
    f.write(0xa000, 1);
    f.write(0x8000, 0x84);
    equal(f.read(0xa000)[0], 3);
    equal(f.read(0xb000)[0], 0x80);
    f.pad.disconnect_pak();
    equal(f.identify(), 2);
    f.pad.transfer_pak();
    equal(f.identify(), 3);
    equal(f.read(0xb000)[0], 0xc0);
    f.write(0xb000, 1);
    equal(f.read(0xb000)[0], 0x8d);
    f.write(0xa000, 0);
    equal(f.read(0xc000)[0], 255);
  }
  {
    std::array<PakFixture, 2> ports;
    for (unsigned port = 0; port < ports.size(); ++port) {
      std::vector<std::uint8_t> rom(0x8000, static_cast<std::uint8_t>(0x31 + port));
      auto cartridge = std::make_shared<HandheldCartridge>();
      equal(cartridge->load(rom, {HandheldCartridge::Board::Linear}), true);
      ports[port].pad.transfer_pak(cartridge);
      equal(ports[port].identify(), 3);
      ports[port].write(0x8000, 0x84);
      ports[port].write(0xb000, 1);
      ports[port].write(0xa000, 0);
    }
    for (unsigned port = 0; port < ports.size(); ++port) {
      equal(ports[port].read(0xb000)[0], 0x8d);
      equal(ports[port].read(0xc000)[0], 0x31 + port);
    }
    ports[0].pad.disconnect_pak();
    equal(ports[0].identify(), 2);
    equal(ports[0].read(0xc000)[0], 0);
    equal(ports[1].read(0xb000)[0], 0x89);
    equal(ports[1].read(0xc000)[0], 0x32);
    ports[0].pad.transfer_pak();
    equal(ports[0].identify(), 3);
    ports[0].write(0xb000, 1);
    equal(ports[0].read(0xc000)[0], 255);
    equal(ports[1].read(0xc000)[0], 0x32);
  }
  {
    PakFixture f;
    f.pad.transfer_pak();
    equal(f.identify(), 3);
    equal(f.read(0xc000)[0], 0);
    f.write(0x8000, 0x84);
    equal(f.read(0xb000)[0], 0xc0);
    equal(f.read(0xc000)[0], 0);
    f.write(0xb000, 1);
    const auto status = f.read(0xb000);
    equal(status[0], 0x8d);
    equal(status[1], 0x89);
    for (unsigned bank = 0; bank < 4; ++bank) {
      f.write(0xa000, static_cast<std::uint8_t>(bank));
      for (unsigned address = 0; address < 0x4000; address += 32) {
        const auto mapped = bank * 0x4000 + address;
        const bool accessible = mapped <= 0x7fff || (mapped >= 0xa000 && mapped <= 0xbfff);
        const auto response = f.read(0xc000 + address);
        for (unsigned byte = 0; byte < 32; ++byte)
          equal(response[byte], accessible ? 255 : 0);
        equal(response[32], data_crc(std::span<const std::uint8_t, 32>(response.data(), 32)));
      }
    }
    f.write(0xb000, 0);
    const auto disabled = f.read(0xb000);
    equal(disabled[0], 0x88);
    equal(disabled[1], 0x84);
    equal(disabled[2], 0x80);
    equal(f.read(0xc000)[0], 0);
    f.write(0x8000, 0xfe);
    equal(f.read(0xb000)[0], 0);
    f.pad.disconnect_pak();
    equal(f.identify(), 2);
    f.pad.transfer_pak();
    equal(f.identify(), 3);
    f.write(0x8000, 0x84);
    equal(f.read(0xb000)[0], 0xc0);
    f.write(0xb000, 1);
    equal(f.read(0xb000)[0], 0x8d);
    equal(f.read(0xc000)[0], 0);
    f.write(0xa000, 0);
    equal(f.read(0xc000)[0], 255);
  }
  {
    PakFixture f;
    std::uint64_t now = 1000000;
    unsigned polls = 0;
    f.pad.bio_sensor([&] {
      ++polls;
      return now;
    });
    equal(f.identify(), 3);
    equal(f.read(0x8000)[0], 0x81);
    equal(polls, 2);
    equal(f.read(0xc000)[0], 0);
    now += 199999;
    equal(f.read(0xc000)[31], 0);
    ++now;
    equal(f.read(0xc000)[0], 3);
    now = 2000000;
    equal(f.read(0xc000)[0], 0);
    f.pad.sensor().beats_per_minute(120);
    now += 200000;
    equal(f.read(0xc000)[0], 3);
    now = 3000000;
    equal(f.read(0xc000)[0], 0);
    now += 200000;
    equal(f.read(0xc000)[0], 3);
    now += 300000;
    equal(f.read(0xc000)[0], 0);
    f.write(0xc000, 0xff);
    equal(f.read(0xc000)[0], 0);
    auto response = f.read(0x8000, 1);
    equal(response[0], 0x81);
    equal(response[1], 0xa5);
    response = f.read(0x8000, 32);
    equal(response[31], 0x81);
    equal(response[32], 0xa5);
    f.pad.disconnect_pak();
    equal(f.identify(), 2);
  }
  {
    Console console;
    boot(console.pif());
    console.connect_mouse(2);
    console.mouse(2).input(true, false, -24, 37);
    const std::array<std::uint8_t, 1> command{1};
    poll(console, 2, command, 6);
    auto ram = console.pif().ram();
    equal(ram[3], 0x46);
    equal(ram[5], 0x80);
    equal(ram[6], 0);
    equal(ram[7], 0xe8);
    equal(ram[8], 0xdb);
    equal(ram[9], 0);
    console.connect_mouse(2, false);
    poll(console, 2, command, 4);
    equal(ram[3], 0x84);
    console.connect_controller(2, true);
    console.controller(2).bio_sensor([] { return 1000000; });
    const std::array<std::uint8_t, 1> identify{0};
    poll(console, 2, identify, 3);
    equal(ram[5], 5);
    equal(ram[7], 3);
    const std::array<std::uint8_t, 3> read{2, 0x80, 1};
    poll(console, 2, read, 33);
    equal(ram[7], 0x81);
    equal(ram[39], data_crc(std::span<const std::uint8_t, 32>(ram.data() + 7, 32)));
  }
}

} // namespace test
