#include "../fixture.hpp"
#include "core/devices/vi/video_interface.hpp"
#include "expected.hpp"
#include "fixtures.hpp"

namespace test {
namespace {

void select_field(cupid::n64::VideoInterface &video, bool target) {
  if (video.field() == target)
    return;
  const auto control = video.read_word(0);
  const auto total = video.read_word(24);
  const auto vertical = video.read_word(40);
  video.write_word(0, 2);
  video.write_word(24, 0);
  video.write_word(40, 1023u << 16);
  video.advance(static_cast<std::uint32_t>(video.clocks() + 1));
  video.write_word(0, control);
  video.write_word(24, total);
  video.write_word(40, vertical);
}

std::uint64_t fingerprint(const cupid::n64::VideoFrame &frame) {
  std::uint64_t value = 14695981039346656037ull;
  for (auto byte : frame.rgba) {
    value ^= byte;
    value *= 1099511628211ull;
  }
  return value;
}

} // namespace

void software_video_tests() {
  using namespace cupid::n64;
  const auto inputs = video::scanout_cases();
  equal(inputs.size() * 6, video::scanout_expected.size());
  for (unsigned id = 0; id < inputs.size(); ++id) {
    const auto &input = inputs[id];
    MemoryFixture memory(input.expansion);
    VideoInterface vi(memory.mi, input.pal ? VideoRegion::Pal : VideoRegion::Ntsc);
    const auto power = [&](bool reset) {
      memory.ram.power(reset);
      memory.ri.power(reset);
      memory.mi.power();
      vi.power();
      memory.initialize();
      for (unsigned n = 0; n < input.registers.size(); ++n)
        vi.write_word(n * 4, input.registers[n]);
    };
    power(false);
    for (unsigned stage = 0; stage < 6; ++stage) {
      if (stage == 4 || stage == 5)
        power(stage == 4);
      if (stage < 4)
        video::fill_framebuffer(input, stage, [&](unsigned address, unsigned value) {
          memory.ram.write(address, 4, value);
        });
      if (stage == 3)
        vi.write_word(0, (input.registers[0] & ~3u) | 1);
      select_field(vi, stage & 1);
      video::Registers before;
      for (unsigned n = 0; n < before.size(); ++n)
        before[n] = vi.read_word(n * 4);
      const auto clocks = vi.clocks();
      const auto fraction = vi.fraction();
      const auto frames = vi.frames();
      const auto irq = memory.mi.read_word(8);
      const auto &frame = vi.scanout(memory.ram);
      const auto &expected = video::scanout_expected.at(id * 6 + stage);
      equal(frame.width, 640);
      equal(frame.height, input.pal ? 576 : 480);
      equal(frame.rgba.size(), std::size_t(frame.width) * frame.height * 4);
      const auto actual = fingerprint(frame);
      if (actual != expected.fingerprint)
        std::cerr << "Software VI case " << id << ", stage " << stage << '\n';
      equal(actual, expected.fingerprint);
      equal(vi.clocks(), expected.clocks);
      equal(vi.fraction(), expected.fraction);
      equal(vi.clocks(), clocks);
      equal(vi.fraction(), fraction);
      equal(vi.frames(), frames);
      equal(memory.mi.read_word(8), irq);
      equal(memory.mi.read_word(8), 55);
      for (unsigned n = 0; n < before.size(); ++n)
        equal(vi.read_word(n * 4), before[n]);
      if (failures)
        return;
    }
  }
  std::cout << "Software VI: " << inputs.size() * 6 << " captures\n";
}

} // namespace test
