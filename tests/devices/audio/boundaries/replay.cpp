#include "../../fixture.hpp"
#include "core/devices/audio/audio_interface.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
#include <bit>

namespace test {
using namespace cupid::n64;

namespace {
struct Fixture : test::MemoryFixture {
  AudioInterface audio;
  explicit Fixture(VideoRegion region) : audio(ram, mi, region) {
    initialize();
    for (unsigned n = 0; n < test::audio_dma::MemoryBytes; ++n)
      ram.write(n, 1, test::audio_dma::pattern(n));
  }
};

struct Probe {
  std::unique_ptr<Fixture> fixture;
  std::uint64_t hash = 0xcbf29ce484222325ull;
  std::uint64_t observations = 0;
  unsigned block = 0;
  void region(VideoRegion value) {
    fixture = std::make_unique<Fixture>(value);
  }
  void reset() {
    power();
    fixture->mi.lower(Interrupt::Audio);
  }
  void power() {
    fixture->mi.power();
    fixture->audio.power();
  }
  void write(unsigned address, unsigned value) {
    fixture->audio.write_word(address, value);
  }
  unsigned read(unsigned address) {
    return fixture->audio.read_word(address);
  }
  unsigned interrupts() {
    return fixture->mi.read_word(8);
  }
  unsigned address(unsigned index) {
    return fixture->audio.state().addresses[index];
  }
  unsigned length(unsigned index) {
    return fixture->audio.state().lengths[index];
  }
  unsigned count() {
    return fixture->audio.state().count;
  }
  bool enabled() {
    return fixture->audio.state().enable;
  }
  bool carry() {
    return fixture->audio.state().carry;
  }
  unsigned dac_rate() {
    return fixture->audio.state().dac_rate;
  }
  unsigned bit_rate() {
    return fixture->audio.state().bit_rate;
  }
  unsigned frequency() {
    return fixture->audio.frequency();
  }
  unsigned precision() {
    return fixture->audio.precision();
  }
  unsigned period() {
    return fixture->audio.period();
  }
  std::uint64_t left() {
    return std::bit_cast<std::uint64_t>(fixture->audio.output().left);
  }
  std::uint64_t right() {
    return std::bit_cast<std::uint64_t>(fixture->audio.output().right);
  }
  std::int64_t clock() {
    return fixture->audio.clocks();
  }
  void sample() {
    fixture->audio.sample();
  }
  void advance(unsigned clocks) {
    fixture->audio.advance(clocks);
  }
  void word(std::uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
      hash ^= (value >> (n * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    test::equal(block < test::audio_dma::expected.size(), true);
    if (block < test::audio_dma::expected.size()) {
      if (hash != test::audio_dma::expected[block])
        std::cerr << "Audio DMA scenario " << block << '\n';
      test::equal(hash, test::audio_dma::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace

void audio_boundary_tests() {
  Probe probe;
  for (auto region : {VideoRegion::Ntsc, VideoRegion::Pal}) {
    probe.region(region);
    test::audio_dma::scenarios(probe);
  }
  test::equal(probe.block, test::audio_dma::expected.size());
  test::equal(probe.observations, 1276416);
}

} // namespace test
