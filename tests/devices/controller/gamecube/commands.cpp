#include "../../fixture.hpp"
#include "core/controller/gamecube/gamecube.hpp"
#include "core/system/console.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

void boot(Pif &pif) {
  pif.tick();
  pif.write_word(0x1fc007fc, 0x10);
  pif.write_word(0x1fc007f0, 0x0000a536);
  pif.write_word(0x1fc007f4, 0xc0f1d859);
  pif.write_word(0x1fc007fc, 0x20);
  pif.write_word(0x1fc007fc, 0x40);
  pif.write_word(0x1fc007fc, 8);
}

void poll(Console &console, unsigned port, std::span<const std::uint8_t> command, unsigned receive,
          unsigned flags = 0) {
  auto ram = console.pif().ram();
  std::fill(ram.begin(), ram.end(), 0);
  ram[port] = static_cast<std::uint8_t>(command.size() | flags);
  ram[port + 1] = static_cast<std::uint8_t>(receive);
  std::copy(command.begin(), command.end(), ram.begin() + port + 2);
  ram[port + command.size() + receive + 2] = 0xfe;
  console.pif().write_word(0x1fc007fc, 1);
  console.pif().dma_read(0x1fc007c0, 0x1000);
}

} // namespace

void gamecube_tests() {
  {
    GameCubePad pad;
    constexpr std::array<std::uint8_t, 3> read{0x40, 3, 1};
    constexpr std::array<std::uint8_t, 1> identify{0}, origin{0x41}, long_read{0x43};
    std::array<std::uint8_t, 10> output{};
    auto status = pad.communicate(read, std::span(output).first(8));
    equal(status.valid, true);
    equal(status.overflow, false);
    constexpr std::array<std::uint8_t, 8> neutral{0x20, 0x80, 127, 127, 127, 127, 0, 0};
    for (unsigned byte = 0; byte < neutral.size(); ++byte)
      equal(output[byte], neutral[byte]);
    pad.communicate(identify, output);
    equal(output[0], 9);
    equal(output[1], 0);
    equal(output[2], 8);
    pad.communicate(origin, {});
    pad.communicate(long_read, output);
    equal(output[0], 0);
    equal(output[1], 0x80);
    equal(output[8], 0);
    equal(output[9], 0);
    equal(pad.rumbling(), true);
    pad.reset();
    equal(pad.rumbling(), false);
    pad.communicate(long_read, output);
    equal(output[0], 0x20);
    pad.input_host({0, 32767, 0, 32767, 0, 32767, 0});
    pad.communicate(long_read, output);
    constexpr std::array<std::uint8_t, 10> cardinal{0x20, 0xc0, 227, 127, 203, 127, 200, 0, 0, 0};
    for (unsigned byte = 0; byte < cardinal.size(); ++byte)
      equal(output[byte], cardinal[byte]);
    const auto before = output;
    equal(pad.communicate({}, output).valid, false);
    for (unsigned byte = 0; byte < output.size(); ++byte)
      equal(output[byte], before[byte]);
  }
  {
    Console console({.random_seed = 0});
    boot(console.pif());
    constexpr std::array<std::uint8_t, 1> identify{0}, origin{0x41};
    constexpr std::array<std::uint8_t, 3> read{0x40, 3, 1};
    constexpr std::array<unsigned, 4> timing{37020, 38440, 39860, 41280};
    for (unsigned port = 0; port < 4; ++port) {
      console.connect_gamecube_controller(port);
      console.gamecube_controller(port).input_host(
          {static_cast<std::uint16_t>(1u << port), 32767, 0});
      poll(console, port, identify, 3);
      auto ram = console.pif().ram();
      equal(ram[port + 1], 3);
      equal(ram[port + 3], 9);
      equal(ram[port + 4], 0);
      equal(console.pif().estimate_timing(), timing[port]);
      poll(console, port, read, 8);
      equal(ram[port + 5], (1u << port) | 0x20);
      std::fill(ram.begin(), ram.end(), 0);
      ram[port] = 0xfd;
      ram[port + 1] = 0xfe;
      console.pif().write_word(0x1fc007fc, 1);
      console.pif().dma_read(0x1fc007c0, 0x1000);
      equal(console.gamecube_controller(port).rumbling(), false);
      poll(console, port, read, 8);
      equal(ram[port + 5], (1u << port) | 0x20);
      equal(ram[port + 7], 227);
      equal(console.gamecube_controller(port).rumbling(), true);
      poll(console, port, origin, 10);
      equal(ram[port + 3], 0);
      equal(ram[port + 5], 127);
      poll(console, port, read, 8);
      equal(ram[port + 5], 1u << port);
      poll(console, port, read, 8, 0x80);
      equal(console.gamecube_controller(port).rumbling(), true);
      poll(console, port, read, 8, 0x40);
      equal(console.gamecube_controller(port).rumbling(), false);
      poll(console, port, read, 8);
      equal(ram[port + 5], (1u << port) | 0x20);
      console.connect_gamecube_controller(port, false);
      equal(console.gamecube_controller(port).rumbling(), false);
      poll(console, port, read, 8);
      equal(ram[port + 1], 0x88);
      console.connect_gamecube_controller(port);
    }
    poll(console, 0, origin, 10);
    poll(console, 0, read, 8);
    for (bool reset : {true, false}) {
      console.power(reset);
      boot(console.pif());
      poll(console, 0, identify, 3);
      equal(console.pif().ram()[5], 8);
      poll(console, 0, read, 8);
      equal(console.pif().ram()[5], 1);
    }
    console.connect_controller(0, true);
    equal(console.gamecube_controller(0).rumbling(), false);
    poll(console, 0, identify, 3);
    equal(console.pif().ram()[3], 5);
    console.connect_gamecube_controller(0);
    poll(console, 0, read, 8);
    console.connect_mouse(0);
    equal(console.gamecube_controller(0).rumbling(), false);
    poll(console, 0, identify, 3);
    equal(console.pif().ram()[3], 2);
    console.connect_gamecube_controller(0);
    poll(console, 0, read, 8);
    equal(console.pif().ram()[5], 0x21);
  }
}

} // namespace test
