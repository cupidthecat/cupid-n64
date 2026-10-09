#include "../fixture.hpp"
#include "bus_expected.hpp"
#include "scenarios.hpp"

namespace test {
using namespace cupid::n64;

namespace {
struct Probe {
  std::unique_ptr<MemoryFixture> f;
  std::uint64_t hash = 0xcbf29ce484222325ull;
  std::uint64_t observations = 0;
  unsigned block = 0;
  unsigned clocks = 0;

  void begin(std::uint64_t seed, bool expansion) {
    f = std::make_unique<MemoryFixture>(expansion);
    f->random.seed(seed);
    f->ram.power();
    f->ri.power();
    f->mi.power();
  }
  void power(bool reset) {
    f->ram.power(reset);
    f->ri.power(reset);
    f->mi.power();
  }
  auto size() const {
    return f->ram.size();
  }
  auto identity() const {
    return f->ram.identity();
  }
  auto active() const {
    return f->ri.active();
  }
  auto random() {
    return f->random();
  }
  void ri_write(unsigned address, unsigned value) {
    f->ri.write_word(address, value);
  }
  auto ri_read(unsigned address) const {
    return f->ri.read_word(address);
  }
  void mi_write(unsigned value) {
    f->mi.write_word(0, value);
  }
  auto mi_read() const {
    return f->mi.read_word(0);
  }
  void chip_write(unsigned address, unsigned value, unsigned repeat = 0) {
    f->ram.write_word(address, value, repeat);
  }
  auto chip_read(unsigned address) {
    return f->ram.read_word(address);
  }
  void store(unsigned address, unsigned bytes, std::uint64_t value) {
    f->ram.write(address, bytes, value);
  }
  void bus_store(unsigned address, unsigned bytes, std::uint64_t value) {
    f->mi.write_rdram(address, bytes, value);
  }
  auto raw(unsigned address, unsigned bytes) {
    return f->ram.read(address, bytes);
  }
  auto load(unsigned address, unsigned bytes) {
    const auto value = f->mi.read_rdram(address, bytes);
    clocks = value.clocks;
    return value.value;
  }
  auto read_clocks() const {
    return clocks;
  }
  auto hidden(unsigned address) const {
    return f->ram.hidden()[address];
  }

  void emit(std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      hash ^= (value >> (byte * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    equal(block < memory_bus::expected.size(), true);
    if (block < memory_bus::expected.size())
      equal(hash, memory_bus::expected[block]);
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace

void memory_bus_tests() {
  Probe p;
  memory_bus::scenarios(p);
  equal(p.block, memory_bus::expected.size());
  equal(p.observations, 13921566);
}

} // namespace test
