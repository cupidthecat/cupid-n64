#include "../../fixture.hpp"
#include "core/devices/pif/pif.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
#include <stdexcept>

namespace test {
using namespace cupid::n64;
namespace {

struct Fixture : test::MemoryFixture {
  Cic cic;
  Pif pif{cic, ram};
  bool nmi = false;
  Fixture() {
    initialize();
    std::array<std::uint8_t, 0x7c0> bytes{};
    for (unsigned n = 0; n < bytes.size(); ++n)
      bytes[n] = test::pif_boot::firmware(n);
    if (!pif.load_rom(bytes))
      throw std::runtime_error("Boot firmware rejected");
    pif.connect_reset([this] {
      nmi = true;
      cpu.request_nmi();
    });
  }
};

struct Probe {
  std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
  unsigned model = 0;
  std::uint64_t hash = 0xcbf29ce484222325ull;
  std::uint64_t observations = 0;
  unsigned block = 0;
  void reset(unsigned index) {
    model = index;
    power();
    for (unsigned n = 0; n < 64; ++n)
      fixture->ram.write(n, 1, 0);
  }
  void power() {
    auto &f = *fixture;
    f.cpu.power();
    f.nmi = false;
    f.cpu.set_pc(0xffffffffa4000000ull);
    f.pif.power();
    f.cic.power(static_cast<CicModel>(model));
  }
  void tick() {
    fixture->pif.tick();
  }
  void challenge(unsigned seed) {
    for (unsigned n = 0; n < 15; ++n)
      fixture->pif.ram()[0x30 + n] = static_cast<std::uint8_t>(n * 29 + seed * 43 + (n >> 1));
    fixture->pif.ram()[63] = 2;
    fixture->pif.dma_read(0x1fc007c0, 0);
  }
  void align(unsigned clocks) {
    fixture->pif.elapse(clocks);
  }
  void advance(unsigned clocks) {
    fixture->pif.advance(clocks);
  }
  void write(unsigned address, unsigned value) {
    fixture->pif.write_word(0x1fc00000 + address, value);
  }
  unsigned read(unsigned address) {
    return fixture->pif.read_word(0x1fc00000 + address);
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
  std::uint64_t memory(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = (value << 8) | fixture->pif.ram()[address + n];
    return value;
  }
  std::uint64_t dma_memory(unsigned address) {
    return fixture->ram.read(address, 8);
  }
  bool poll() {
    if (!fixture->nmi)
      return false;
    fixture->cpu.step();
    return true;
  }
  std::uint64_t pc() {
    return fixture->cpu.state().pc;
  }
  std::uint64_t status() {
    return fixture->cpu.read_control(Status);
  }
  std::uint64_t error_epc() {
    return fixture->cpu.read_control(ErrorEpc);
  }
  std::uint64_t cpu_clocks() {
    return fixture->cpu.state().clocks;
  }
  void emit(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
      hash ^= (value >> (n * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    test::equal(block < test::pif_boot::expected.size(), true);
    if (block < test::pif_boot::expected.size()) {
      if (hash != test::pif_boot::expected[block])
        std::cerr << "PIF boot scenario " << block << '\n';
      test::equal(hash, test::pif_boot::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace

void pif_boot_tests() {
  Probe probe;
  test::pif_boot::scenarios(probe);
  test::equal(probe.block, test::pif_boot::expected.size());
  test::equal(probe.observations, 49005);
}
} // namespace test
