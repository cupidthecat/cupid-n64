#include "../fixture.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr unsigned write_clocks = 1125000;

struct EepromReset {
  std::unique_ptr<Console> console;
  unsigned size;
  explicit EepromReset(unsigned capacity) : size(capacity) {
    ConsoleConfig config;
    config.random_seed = 0;
    config.eeprom_size = size;
    console = std::make_unique<Console>(config);
  }
  void advance(unsigned clocks) {
    console->cpu().advance_clocks(clocks);
    console->synchronize();
  }
  void write(unsigned base, bool busy) {
    std::array<std::uint8_t, 10> input{5, 3};
    std::array<std::uint8_t, 1> output{};
    for (unsigned n = 2; n < input.size(); ++n)
      input[n] = static_cast<std::uint8_t>(base + n - 2);
    equal(console->eeprom().communicate(input, output).valid, true);
    equal(output[0], busy ? 0x80 : 0);
  }
  void state(unsigned base, bool busy) {
    for (unsigned command : {0u, 255u}) {
      std::array<std::uint8_t, 1> input{static_cast<std::uint8_t>(command)};
      std::array<std::uint8_t, 3> output{};
      equal(console->eeprom().communicate(input, output).valid, true);
      equal(output[0], 0);
      equal(output[1], size == 512 ? 0x80 : 0xc0);
      equal(output[2], busy ? 0x80 : 0);
    }
    std::array<std::uint8_t, 2> input{4, 3};
    std::array<std::uint8_t, 8> output{};
    equal(console->eeprom().communicate(input, output).valid, true);
    for (unsigned n = 0; n < output.size(); ++n) {
      const auto value = base ? base + n : 255;
      equal(console->eeprom().data()[24 + n], value);
      equal(output[n], busy ? 255 : value);
    }
  }
};

void reset_write(unsigned size, bool warm, unsigned boundary) {
  EepromReset f(size);
  f.write(1, false);
  f.advance(boundary);
  const bool busy = boundary < write_clocks;
  f.state(1, busy);
  f.console->power(warm);
  f.state(1, busy);
  f.write(9, busy);
  f.state(busy ? 1 : 9, true);
  f.advance(write_clocks * 2);
  f.state(busy ? 1 : 9, busy);
  f.console->power(!warm);
  f.state(busy ? 1 : 9, busy);
  f.write(17, busy);
  f.advance(write_clocks);
  f.state(busy ? 1 : 17, busy);
}

} // namespace

void eeprom_reset_tests() {
  for (unsigned size : {512u, 2048u}) {
    for (bool warm : {false, true}) {
      for (unsigned boundary : {0u, write_clocks - 1, write_clocks, write_clocks + 1})
        reset_write(size, warm, boundary);
      EepromReset idle(size);
      idle.console->power(warm);
      idle.state(0, false);
      idle.write(1, false);
      idle.advance(write_clocks - 1);
      idle.state(1, true);
      idle.advance(1);
      idle.state(1, false);
    }
  }
}

} // namespace test
