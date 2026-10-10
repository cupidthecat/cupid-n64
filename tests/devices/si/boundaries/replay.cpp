#include "../../fixture.hpp"
#include "core/devices/si/serial_interface.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct Fixture : MemoryFixture {
  Cic cic;
  Pif pif{cic, ram};
  EventQueue events;
  SerialInterface si{pif, mi, events};
  Fixture() {
    initialize();
  }
};
struct Probe {
  std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
  std::vector<unsigned> events;
  std::uint64_t hash = 0xcbf29ce484222325ull;
  std::uint64_t observations = 0;
  unsigned block = 0;
  void reset(unsigned epoch) {
    auto &f = *fixture;
    f.events.reset();
    f.events.advance(epoch, [](Event) {});
    f.si.power();
    f.mi.power();
    f.cic.power(CicModel::N6102);
    f.pif.power();
    f.pif.tick();
    f.pif.write_word(0x1fc007fc, 0x10);
    f.pif.write_word(0x1fc007f0, 0x0000a536);
    f.pif.write_word(0x1fc007f4, 0xc0f1d859);
    f.pif.write_word(0x1fc007fc, 0x20);
    f.pif.write_word(0x1fc007fc, 0x40);
    f.pif.write_word(0x1fc007fc, 8);
    for (unsigned n = 0; n < si_dma::MemoryBytes; ++n)
      f.ram.write(n, 1, si_dma::pattern(n));
    for (unsigned n = 0; n < 64; ++n)
      f.pif.ram()[n] = si_dma::packet(n);
    f.mi.lower(Interrupt::Serial);
    events.clear();
  }
  void power() {
    fixture->events.reset();
    fixture->si.power();
    fixture->mi.power();
  }
  unsigned read(unsigned offset) {
    return fixture->si.read_io(offset);
  }
  void write(unsigned offset, unsigned value) {
    fixture->si.write_io(offset, value);
  }
  unsigned interrupts() {
    return fixture->mi.read_word(8);
  }
  auto deadline() {
    return fixture->events.time_to_event();
  }
  unsigned state() {
    return static_cast<unsigned>(fixture->pif.state());
  }
  bool reset_enabled() {
    return fixture->pif.reset_enabled();
  }
  unsigned timing() {
    return fixture->pif.estimate_timing();
  }
  void prepare_command(unsigned channel, unsigned send, unsigned receive, unsigned padding) {
    auto &pif = fixture->pif;
    std::fill(pif.ram().begin(), pif.ram().end(), 0xa5);
    for (unsigned n = 0; n < channel; ++n)
      pif.ram()[n] = 0;
    for (unsigned n = channel; n < channel + padding; ++n)
      pif.ram()[n] = 0xff;
    const auto start = channel + padding;
    pif.ram()[start] = static_cast<std::uint8_t>(send);
    pif.ram()[start + 1] = static_cast<std::uint8_t>(receive);
    pif.ram()[start + 2] = 1;
    const auto end = start + 2 + (send & 63) + (receive & 63);
    if (end < 63)
      pif.ram()[end] = 0xfe;
    pif.write_word(0x1fc007fc, (std::uint32_t(pif.ram()[60]) << 24) |
                                   (std::uint32_t(pif.ram()[61]) << 16) |
                                   (std::uint32_t(pif.ram()[62]) << 8) | 1);
  }
  std::uint64_t pif_memory(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | fixture->pif.ram()[address + n];
    return value;
  }
  std::uint64_t memory(unsigned address) {
    return fixture->ram.read(address, 8);
  }
  std::uint64_t coverage(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | fixture->ram.hidden()[address + n];
    return value;
  }
  std::uint64_t bus_read(unsigned address, unsigned bytes) {
    return fixture->si.read(address, bytes).value;
  }
  void bus_write(unsigned address, unsigned bytes, std::uint64_t value) {
    fixture->si.write(address, bytes, value);
  }
  void advance(unsigned clocks) {
    fixture->events.advance(clocks, [&](Event event) {
      if (event == Event::SerialRead) {
        events.push_back(1);
        fixture->si.dma_read();
      } else if (event == Event::SerialWrite) {
        events.push_back(2);
        fixture->si.dma_write();
      } else if (event == Event::SerialBusWrite) {
        events.push_back(3);
        fixture->si.complete_write();
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
    equal(block < si_dma::expected.size(), true);
    if (block < si_dma::expected.size()) {
      if (hash != si_dma::expected[block])
        std::cerr << "SI DMA scenario " << block << '\n';
      equal(hash, si_dma::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace

void si_boundary_tests() {
  Probe probe;
  si_dma::scenarios(probe);
  equal(probe.block, si_dma::expected.size());
  equal(probe.observations, 4541540);
}

} // namespace test
