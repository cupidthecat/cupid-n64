#include "fixture.hpp"
#include "video_expected.hpp"

namespace test {

void gpu_video_replay_tests() {
  using namespace renderer_replay;
  GpuFixture fixture;
  unsigned id = 0, snapshot = 0;
  auto check = [&](unsigned stage, bool field) {
    const auto before = failures;
    const auto &wanted = expected::video[snapshot++];
    const auto frame = fixture.renderer->frame(field);
    equal(frame.width, wanted.width);
    equal(frame.height, wanted.height);
    equal(frame.rgba.size(), std::size_t(wanted.width) * wanted.height * 4);
    equal(fingerprint(frame.rgba), wanted.pixels);
    for (unsigned n = 0; n < wanted.registers.size(); ++n)
      equal(fixture.console.video().read_word(n * 4), wanted.registers[n]);
    equal(fixture.irq(), wanted.irq);
    if (failures != before) {
      std::cerr << "VI case " << id << " stage " << stage << " field " << field << '\n';
      for (unsigned n = 0; n < wanted.registers.size(); ++n)
        std::cerr << "VI[" << n << "]=0x" << std::hex << fixture.console.video().read_word(n * 4)
                  << std::dec << '\n';
    }
    return failures == before;
  };
  if (!check(0, false))
    return;
  ++id;
  for (unsigned format : {2u, 3u}) {
    for (unsigned n = 0; n < 0x80000; n += 4)
      fixture.put(0x100000 + n, 0);
    for (unsigned y = 0; y < 512; ++y)
      for (unsigned x = 0; x < 64; x += 2) {
        if (format == 2)
          fixture.put(0x100000 + (y * 64 + x) * 2,
                      (std::uint32_t(pixel16(x, y)) << 16) | pixel16(x + 1, y));
        else {
          fixture.put(0x100000 + (y * 64 + x) * 4, pixel32(x, y));
          fixture.put(0x100000 + (y * 64 + x + 1) * 4, pixel32(x + 1, y));
        }
      }
    for (unsigned n = 0; n < 0x40000; ++n)
      fixture.hidden(0x80000 + n, (n * 3 + 1) & 3);
    for (const auto &registers : video_variants(format)) {
      fixture.program(base_video(format));
      if (!check(0, false))
        return;
      fixture.program(registers);
      if (!check(1, false) || !check(2, true))
        return;
      fixture.program(base_video(format));
      if (!check(3, false))
        return;
      ++id;
    }
    for (bool warm : {true, false}) {
      fixture.power(warm);
      if (!check(0, false))
        return;
      fixture.program(base_video(format));
      if (!check(1, false) || !check(2, true))
        return;
      ++id;
    }
  }
  equal(snapshot, std::size(expected::video));
  std::cout << "VI register replay: " << snapshot << " captures\n";
}

} // namespace test
