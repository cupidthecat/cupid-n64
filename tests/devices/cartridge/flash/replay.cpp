#include "../../fixture.hpp"
#include "core/cartridge/flash/flash.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
#include <memory>
#include <stdexcept>
#include <vector>
namespace test {
using namespace cupid::n64;
namespace {
struct Probe {
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;
  unsigned block = 0;
  EventQueue events;
  std::unique_ptr<FlashRam> flash;
  std::vector<unsigned> fired;
  void reset(unsigned model, unsigned epoch) {
    fired.clear();
    events.reset();
    events.advance(epoch, [](Event) {});
    flash = std::make_unique<FlashRam>(events, static_cast<FlashModel>(model));
    for (unsigned n = 0; n < flash_boundary::Bytes; ++n)
      flash->data()[n] = flash_boundary::pattern(n);
  }
  bool select(unsigned address) {
    return flash->select(address, {});
  }
  unsigned read_half() {
    const auto value = flash->read_half({});
    return value ? unsigned(*value) : 0x10000;
  }
  void write_half(std::uint16_t value) {
    flash->write_half(value, {});
  }
  unsigned byte(unsigned address) {
    return flash->data()[address];
  }
  auto deadline() {
    return events.time_to_event();
  }
  void markers(unsigned clocks) {
    events.insert(Event::PeripheralRead, clocks);
    events.insert(Event::SerialRead, clocks);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      if (event == Event::FlashComplete) {
        fired.push_back(1);
        flash->complete();
      } else if (event == Event::PeripheralRead)
        fired.push_back(2);
      else if (event == Event::SerialRead)
        fired.push_back(3);
      else
        throw std::runtime_error("Unexpected Flash boundary event");
    });
  }
  void power(bool) {
    events.reset();
    flash->power();
  }
  void word(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
      hash ^= (value >> (n * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    equal(block < flash_boundary::expected.size(), true);
    if (block < flash_boundary::expected.size()) {
      if (hash != flash_boundary::expected[block])
        std::cerr << "Flash boundary scenario " << block << '\n';
      equal(hash, flash_boundary::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace
void flash_boundary_tests() {
  Probe probe;
  flash_boundary::scenarios(probe);
  equal(probe.block, flash_boundary::expected.size());
  equal(probe.observations, 166884480);
}
} // namespace test
