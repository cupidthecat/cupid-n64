#include "../../fixture.hpp"
#include "core/devices/pi/peripheral_interface.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct Cartridge : PeripheralDevice {
  std::array<std::uint8_t, pi_dma::CartridgeBytes> bytes{};
  unsigned offset = 0;
  bool select(std::uint32_t address, PeripheralTiming) override {
    if ((address & 0xff000000) != 0x08000000 && (address & 0xff000000) != 0x10000000)
      return false;
    offset = address & 0x00ffffff;
    return true;
  }
  std::optional<std::uint16_t> read_half(PeripheralTiming) override {
    if (offset + 1 >= bytes.size())
      return {};
    const auto value =
        static_cast<std::uint16_t>((unsigned(bytes[offset]) << 8) | bytes[offset + 1]);
    offset += 2;
    return value;
  }
  void write_half(std::uint16_t value, PeripheralTiming) override {
    if (offset + 1 >= bytes.size())
      return;
    bytes[offset++] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset++] = static_cast<std::uint8_t>(value);
  }
};

struct Fixture : MemoryFixture {
  EventQueue events;
  PeripheralInterface pi{ram, mi, events};
  Cartridge cartridge;
  Fixture() {
    initialize();
    pi.attach(cartridge, 0);
  }
};

struct Probe {
  std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
  std::vector<unsigned> events;
  std::uint64_t hash = 0xcbf29ce484222325ull;
  std::uint64_t observations = 0;
  unsigned block = 0;
  void reset(unsigned epoch) {
    fixture->events.reset();
    fixture->pi.power();
    fixture->mi.power();
    fixture->events.advance(epoch, [](Event) {});
    for (unsigned n = 0; n < pi_dma::MemoryBytes; ++n)
      fixture->ram.write(n, 1, pi_dma::ram_pattern(n));
    for (unsigned n = 0; n < fixture->cartridge.bytes.size(); ++n)
      fixture->cartridge.bytes[n] = pi_dma::cartridge_pattern(n);
    fixture->mi.lower(Interrupt::Peripheral);
    events.clear();
  }
  void power() {
    fixture->events.reset();
    fixture->pi.power();
    fixture->mi.power();
  }
  unsigned read(unsigned offset) {
    return fixture->pi.read_io(offset);
  }
  void write(unsigned offset, unsigned value) {
    fixture->pi.write_io(offset, value);
  }
  unsigned interrupts() {
    return fixture->mi.read_word(8);
  }
  auto deadline() {
    return fixture->events.time_to_event();
  }
  auto memory(unsigned address) {
    return fixture->ram.read(address, 8);
  }
  std::uint64_t coverage(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | fixture->ram.hidden()[address + n];
    return value;
  }
  std::uint64_t cartridge(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | fixture->cartridge.bytes[address + n];
    return value;
  }
  void advance(unsigned clocks) {
    fixture->events.advance(clocks, [&](Event event) {
      if (event == Event::PeripheralRead || event == Event::PeripheralWrite) {
        events.push_back(event == Event::PeripheralRead ? 1 : 2);
        fixture->pi.complete_dma();
      } else if (event == Event::PeripheralBusWrite) {
        events.push_back(3);
        fixture->pi.complete_write();
      } else
        equal(true, false);
    });
  }
  void emit(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
      hash ^= (value >> (n * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    equal(block < pi_dma::expected.size(), true);
    if (block < pi_dma::expected.size()) {
      if (hash != pi_dma::expected[block])
        std::cerr << "PI DMA scenario " << block << '\n';
      equal(hash, pi_dma::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};

} // namespace

void pi_boundary_tests() {
  Probe probe;
  pi_dma::scenarios(probe);
  equal(probe.block, pi_dma::expected.size());
  equal(probe.observations, 3918920);
}

} // namespace test
