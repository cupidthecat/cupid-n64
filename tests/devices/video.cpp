#include "core/devices/vi/video_interface.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void video_tests() {
  MemoryFixture f;
  VideoInterface video(f.mi);
  constexpr std::uint32_t masks[] = {
      0xffff,     0xffffff,   0xfff,      0x3ff,      0,          0x3fffffff, 0x3ff, 0x001f0fff,
      0x0fff0fff, 0x03ff03ff, 0x03ff03ff, 0x03ff03ff, 0x0fff0fff, 0x0fff0fff, 0,     0};
  equal(video.read_word(12), 256);
  for (unsigned n = 0; n < 16; ++n) {
    video.write_word(n * 4, 0xffffffff);
    equal(video.read_word(n * 4), masks[n]);
  }
  video.power();
  video.advance(1);
  equal(video.read_word(16), 0);
  equal(video.frames(), 0);
  equal(video.clocks(), (2048ull * clock_frequency / video_frequency(VideoRegion::Ntsc)) - 1);
  equal(video.fraction(), 2048ull * clock_frequency % video_frequency(VideoRegion::Ntsc));
  unsigned frames = 0;
  video.connect_frame([&](bool field) {
    ++frames;
    equal(field, false);
  });
  video.power();
  video.write_word(0, 2);
  video.write_word(24, 525);
  video.write_word(28, 3092);
  video.write_word(32, 3093 | (3093u << 16));
  video.write_word(12, 20);
  video.write_word(40, 20u << 16);
  for (unsigned n = 1; n <= 263; ++n) {
    video.advance(static_cast<std::uint32_t>(video.clocks() + 1));
    equal(video.read_word(16), n == 263 ? 0 : n * 2);
    if (n == 9)
      equal(f.mi.read_word(8) & 8, 0);
    if (n == 10)
      equal(f.mi.read_word(8) & 8, 8);
  }
  equal(frames, 1);
  equal(video.frames(), 1);
  video.write_word(16, 0);
  equal(f.mi.read_word(8) & 8, 0);
  video.power();
  video.connect_frame({});
  video.write_word(0, 2);
  video.write_word(24, 524);
  video.write_word(28, 3092);
  video.write_word(32, 3093 | (3093u << 16));
  video.write_word(12, 0);
  for (unsigned n = 0; n < 262; ++n)
    video.advance(static_cast<std::uint32_t>(video.clocks() + 1));
  equal(video.read_word(16), 524);
  equal(f.mi.read_word(8) & 8, 8);
  video.write_word(16, 0);
  video.advance(static_cast<std::uint32_t>(video.clocks() + 1));
  equal(video.read_word(16), 1);
  equal(video.field(), true);
  for (unsigned n = 0; n < 262; ++n)
    video.advance(static_cast<std::uint32_t>(video.clocks() + 1));
  equal(video.read_word(16), 0);
  equal(video.field(), false);
  unsigned writes = 0;
  video.connect_registers([&](unsigned index, std::uint32_t value) {
    ++writes;
    equal(index, 15);
    equal(value, 0x12345678);
  });
  video.write_word(60, 0x12345678);
  equal(writes, 1);
  video.connect_sync([&] { ++writes; });
  video.read_word(16);
  equal(writes, 2);
  VideoInterface pal(f.mi, VideoRegion::Pal);
  pal.advance(1);
  equal(pal.clocks(), (2048ull * clock_frequency / video_frequency(VideoRegion::Pal)) - 1);
  equal(pal.fraction(), 2048ull * clock_frequency % video_frequency(VideoRegion::Pal));
}

} // namespace test
