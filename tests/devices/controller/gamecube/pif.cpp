#include "../../fixture.hpp"
#include "core/system/console.hpp"
#include "expected.hpp"
#include "routes.hpp"
#include <algorithm>

namespace test {
namespace {

struct Probe {
  cupid::n64::Console console{{.random_seed = 0}};
  unsigned block = 0;
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;

  Probe() {
    boot();
  }
  void boot() {
    auto &pif = console.pif();
    pif.tick();
    pif.write_word(0x1fc007fc, 0x10);
    pif.write_word(0x1fc007f0, 0xa536);
    pif.write_word(0x1fc007f4, 0xc0f1d859);
    pif.write_word(0x1fc007fc, 0x20);
    pif.write_word(0x1fc007fc, 0x40);
    pif.write_word(0x1fc007fc, 8);
  }
  void power(bool reset) {
    console.power(reset);
    boot();
  }
  void reset(unsigned port) {
    console.gamecube_controller(port).reset();
  }
  void select(unsigned port, unsigned type) {
    if (type == 0)
      console.connect_gamecube_controller(port, false);
    if (type == 1)
      console.connect_controller(port, true);
    if (type == 2)
      console.connect_mouse(port);
    if (type == 3) {
      console.connect_gamecube_controller(port);
      console.gamecube_controller(port).input_host(
          {static_cast<std::uint16_t>(1 << port), 17000, -25000, -29000, 9000, 12000, 26000});
    }
  }
  void emit(std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      hash ^= (value >> (byte * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void run() {
    auto &pif = console.pif();
    emit(pif.estimate_timing());
    pif.write_word(0x1fc007fc, 1);
    pif.dma_read(0x1fc007c0, 0x1000);
    for (auto byte : pif.ram())
      emit(byte);
  }
  void command(unsigned port, std::span<const std::uint8_t> input, unsigned receive,
               unsigned flags = 0) {
    auto ram = console.pif().ram();
    std::fill(ram.begin(), ram.end(), 0);
    ram[port] = static_cast<std::uint8_t>(input.size() | flags);
    ram[port + 1] = static_cast<std::uint8_t>(receive);
    std::copy(input.begin(), input.end(), ram.begin() + port + 2);
    ram[port + 2 + input.size() + receive] = 0xfe;
    run();
  }
  void short_reset(unsigned port) {
    auto ram = console.pif().ram();
    std::fill(ram.begin(), ram.end(), 0);
    ram[port] = 0xfd;
    ram[port + 1] = 0xfe;
    run();
  }
  void finish() {
    equal(block < gamecube::route_expected.size(), true);
    if (block < gamecube::route_expected.size())
      equal(hash, gamecube::route_expected[block]);
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};

} // namespace

void gamecube_route_tests() {
  Probe p;
  gamecube::route_scenarios(p);
  equal(p.block, gamecube::route_expected.size());
  equal(p.observations, gamecube::route_observations);
}

} // namespace test
