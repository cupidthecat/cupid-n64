#include "core/devices/audio/audio_interface.hpp"
#include "fixture.hpp"
#include <bit>

namespace test {
using namespace cupid::n64;

void audio_tests() {
  MemoryFixture f;
  f.initialize();
  AudioInterface audio(f.ram, f.mi);
  const auto bits = [](double value) { return std::bit_cast<std::uint64_t>(value); };
  equal(audio.frequency(), 44100);
  equal(audio.precision(), 16);
  equal(audio.period(), clock_frequency / 44100);
  equal(audio.read_word(12), 0x01100000);
  f.ram.write(0x1ff8, 4, 0x80007fff);
  f.ram.write(0x1ffc, 4, 0x4000c000);
  f.ram.write(0x5000, 4, 0x12345678);
  f.ram.write(0x7000, 4, 0x00010002);
  audio.write_word(0, 0x1fff);
  audio.write_word(4, 15);
  equal(audio.state().addresses[0], 0x1ff8);
  equal(audio.read_word(4), 8);
  equal(f.mi.read_word(8) & 4, 4);
  audio.write_word(12, 0);
  equal(f.mi.read_word(8) & 4, 0);
  audio.write_word(0, 0x5000);
  audio.write_word(4, 8);
  equal(audio.read_word(12), 0xc1100001);
  audio.write_word(0, 0x6000);
  audio.write_word(4, 24);
  equal(audio.state().count, 2);
  equal(audio.state().addresses[1], 0x5000);
  equal(audio.sample().left == 0, true);
  equal(audio.state().lengths[0], 8);
  audio.write_word(8, 1);
  const auto first = audio.sample();
  equal(bits(first.left), bits(-1.0));
  equal(bits(first.right), bits(32767.0 / 32768));
  const auto second = audio.sample();
  equal(bits(second.left), bits(0.5));
  equal(bits(second.right), bits(-0.5));
  equal(audio.state().count, 1);
  equal(audio.state().carry, true);
  equal(audio.state().addresses[0], 0x5000);
  equal(f.mi.read_word(8) & 4, 4);
  audio.sample();
  equal(audio.state().addresses[0], 0x7004);
  equal(bits(audio.output().left), bits(1.0 / 32768));
  equal(bits(audio.output().right), bits(2.0 / 32768));
  audio.sample();
  equal(audio.state().count, 0);
  audio.write_word(0, 0xfffffff8);
  audio.write_word(4, 8);
  equal(audio.state().addresses[0], 0xfffff8);
  audio.sample();
  audio.sample();
  equal(audio.state().addresses[0], 0xffe000);
  equal(audio.state().carry, true);

  audio.power();
  audio.write_word(4, 0);
  equal(audio.state().count, 1);
  audio.sample();
  equal(audio.state().count, 0);
  unsigned frequencies = 0;
  unsigned samples = 0;
  unsigned last_frequency = 0;
  audio.connect([&](StereoSample) { ++samples; },
                [&](unsigned rate) {
                  ++frequencies;
                  last_frequency = rate;
                });
  equal(last_frequency, 44100);
  audio.write_word(16, 1103);
  equal(audio.frequency(), 48681818 / 1104);
  equal(frequencies, 2);
  audio.write_word(16, 1103);
  equal(frequencies, 2);
  audio.write_word(20, 0xffffff03);
  equal(audio.precision(), 4);
  audio.advance(audio.period() * 3);
  equal(samples, 3);
  equal(audio.clocks(), 0);
  audio.advance(1);
  equal(samples, 4);
  equal(audio.clocks(), audio.period() - 1);
  audio.connect_sync([&] { ++samples; });
  audio.read_word(12);
  equal(samples, 5);
  for (unsigned n = 0; n < 8; ++n) {
    if (n != 3)
      equal(audio.read_word(n * 4), audio.state().lengths[0]);
  }
  AudioInterface pal(f.ram, f.mi, VideoRegion::Pal);
  pal.write_word(16, 1103);
  equal(pal.frequency(), 49656530 / 1104);
  equal(pal.period(), clock_frequency / pal.frequency());

  audio.power();
  f.ram.write(0x1000, 4, 0x7fff8000);
  f.ram.write(0x1004, 4, 0x7fff8000);
  audio.write_word(0, 0x1000);
  audio.write_word(4, 8);
  audio.write_word(8, 1);
  audio.sample();
  audio.sample();
  const auto old = audio.output();
  const auto decay = audio.sample();
  equal(decay.left > 0 && decay.left < old.left, true);
  equal(decay.right < 0 && decay.right > old.right, true);
  for (unsigned n = 0; n < 3000; ++n)
    audio.sample();
  equal(bits(audio.output().left), bits(0.0));
  equal(bits(audio.output().right), bits(0.0));
}

} // namespace test
