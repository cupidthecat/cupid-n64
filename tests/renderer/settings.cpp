#include "../support/test.hpp"
#include "core/state/core_state.hpp"
#include "renderer/state/machine_state.hpp"
#include "replay/commands.hpp"
#include "replay/fixture.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

using namespace cupid::n64;
using namespace test::renderer_replay;

namespace {

void recreate(GpuFixture &fixture, RenderSettings settings) {
  fixture.renderer.reset();
  fixture.renderer = std::make_unique<HardwareRenderer>(fixture.console.ram(), settings);
}

void fill(GpuFixture &fixture, std::uint32_t color) {
  constexpr std::uint32_t extent = (63u * 4 << 12) | (255u * 4);
  const std::array<std::uint32_t, 12> commands{
      0x3f10003f,          0x100000, 0x2d000000, (64u * 4 << 12) | (256u * 4),
      0x2f300000,          0,        0x37000000, color,
      0x36000000 | extent, 0,        0x29000000, 0};
  for (std::size_t offset = 0; offset < commands.size(); offset += 2)
    fixture.renderer->submit(std::span(commands).subspan(offset, 2));
  fixture.renderer->synchronize();
}

void dimensions(const VideoFrame &frame, unsigned scale, unsigned height = 240) {
  test::equal(frame.width, 640 * scale);
  test::equal(frame.height, height * scale);
  test::equal(frame.rgba.size(), std::size_t(640) * height * scale * scale * 4);
}

void modes() {
  std::vector<std::uint32_t> native_memory;
  std::vector<std::uint8_t> native_hidden;
  std::uint64_t native_pixels = 0;
  for (const auto scale : RenderSettings::scales) {
    GpuFixture fixture;
    const RenderSettings selected{.scale = scale};
    recreate(fixture, selected);
    fixture.program(base_video(2));
    fill(fixture, 0xf801f801);
    test::equal(fixture.console.ram().read(0x100000, 4), 0xf801f801);
    const auto core = CoreState::capture(fixture.console);
    const auto frame = fixture.renderer->frame(false);
    dimensions(frame, scale);
    test::equal(CoreState::capture(fixture.console) == core, true);
    const auto memory = std::as_const(fixture.console.ram()).words();
    const auto hidden = fixture.console.ram().hidden();
    if (scale == 1) {
      native_memory.assign(memory.begin(), memory.end());
      native_hidden.assign(hidden.begin(), hidden.end());
      native_pixels = fingerprint(frame.rgba);
    } else {
      test::equal(std::equal(memory.begin(), memory.end(), native_memory.begin()), true);
      test::equal(std::equal(hidden.begin(), hidden.end(), native_hidden.begin()), true);
    }
    bool rejected = false;
    try {
      fixture.renderer->scanout_settings({.scale = scale == 1 ? 2u : 1u});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    test::equal(rejected, true);
    test::equal(fixture.renderer->settings() == selected, true);
    test::equal(CoreState::capture(fixture.console) == core, true);
    if (scale > 1) {
      auto sampled = selected;
      sampled.supersampling = true;
      fixture.renderer->scanout_settings(sampled);
      const auto reduced = fixture.renderer->frame(false);
      dimensions(reduced, 1);
      test::equal(CoreState::capture(fixture.console) == core, true);
      fixture.renderer->scanout_settings(selected);
    }
    if (scale <= 2) {
      const auto snapshot = MachineState::capture(fixture.console, *fixture.renderer);
      fill(fixture, 0x003f003f);
      test::equal(fingerprint(fixture.renderer->frame(false).rgba) != fingerprint(frame.rgba),
                  true);
      MachineState::restore(fixture.console, *fixture.renderer, snapshot);
      test::equal(CoreState::capture(fixture.console) == core, true);
      test::equal(fixture.renderer->frame(false).rgba == frame.rgba, true);
    }
    fixture.power(true);
    recreate(fixture, selected);
    fixture.program(base_video(2));
    fill(fixture, 0xf801f801);
    const auto reset = fixture.renderer->frame(false);
    dimensions(reset, scale);
    test::equal(reset.rgba == frame.rgba, true);
    if (scale == 1)
      test::equal(fingerprint(reset.rgba), native_pixels);
    std::cout << scale << "x scanout, native RDRAM/coverage and recreation passed\n";
  }
}

void processing() {
  GpuFixture fixture;
  for (unsigned y = 0; y < 512; ++y)
    for (unsigned x = 0; x < 64; x += 2)
      fixture.put(0x100000 + (y * 64 + x) * 2,
                  (std::uint32_t(pixel16(x, y)) << 16) | pixel16(x + 1, y));
  auto registers = base_video(2);
  registers[0] |= 0x1001c;
  fixture.program(registers);
  const auto guest = CoreState::capture(fixture.console);
  const auto processed = fixture.renderer->frame(false);
  dimensions(processed, 1);
  fixture.renderer->scanout_settings({.bypass_vi = true});
  const auto bypassed = fixture.renderer->frame(false);
  dimensions(bypassed, 1);
  test::equal(fingerprint(processed.rgba) != fingerprint(bypassed.rgba), true);
  test::equal(CoreState::capture(fixture.console) == guest, true);
  fixture.renderer->scanout_settings({.weave = true});
  dimensions(fixture.renderer->frame(false), 1);
  test::equal(CoreState::capture(fixture.console) == guest, true);
  registers[0] |= 0x40;
  registers[6] = 524;
  fixture.program(registers);
  const auto interlaced = CoreState::capture(fixture.console);
  const auto first = fixture.renderer->frame(false);
  const auto second = fixture.renderer->frame(true);
  dimensions(first, 1, 480);
  dimensions(second, 1, 480);
  test::equal(first.rgba != second.rgba, true);
  test::equal(CoreState::capture(fixture.console) == interlaced, true);
  fixture.renderer->scanout_settings({});
  dimensions(fixture.renderer->frame(false), 1);
  test::equal(CoreState::capture(fixture.console) == interlaced, true);
  std::cout << "Progressive/interlaced VI processing and weave preserve guest state\n";
}

void textured_readback() {
  std::vector<std::uint32_t> native_memory;
  std::vector<std::uint8_t> native_hidden;
  for (const auto scale : RenderSettings::scales) {
    GpuFixture fixture;
    recreate(fixture, {.scale = scale});
    const RenderCase scene{"shaded-texture", 3, 14, 1, 2};
    initialize_render(fixture, scene);
    unsigned draw_start = 0;
    auto packets = render_commands(scene, draw_start);
    packets.push_back({0x29000000, 0});
    unsigned end = 0x2000;
    for (const auto &packet : packets)
      for (const auto word : packet) {
        fixture.command_word(end, word, false);
        end += 4;
      }
    fixture.display_write(0, 0x2000);
    fixture.display_write(1, end);
    fixture.console.display().advance(31);
    fixture.renderer->synchronize();
    const auto frame = fixture.renderer->frame(false);
    dimensions(frame, scale);
    const auto memory = std::as_const(fixture.console.ram()).words();
    const auto hidden = fixture.console.ram().hidden();
    test::equal(fixture.crashed(), false);
    if (scale == 1) {
      native_memory.assign(memory.begin(), memory.end());
      native_hidden.assign(hidden.begin(), hidden.end());
    } else {
      test::equal(std::equal(memory.begin(), memory.end(), native_memory.begin()), true);
      test::equal(std::equal(hidden.begin(), hidden.end(), native_hidden.begin()), true);
    }
    if (scale == 2) {
      const auto snapshot = MachineState::capture(fixture.console, *fixture.renderer);
      fill(fixture, 0x003f003f);
      fixture.renderer->frame(false);
      MachineState::restore(fixture.console, *fixture.renderer, snapshot);
      test::equal(fixture.renderer->frame(false).rgba == frame.rgba, true);
      recreate(fixture, {.scale = scale});
      MachineState::restore(fixture.console, *fixture.renderer, snapshot);
      test::equal(fixture.renderer->frame(false).rgba == frame.rgba, true);
    }
  }
  std::cout << "Textured upscaling keeps native framebuffer/coverage and restores subpixels\n";
}

void interlaced_supersampling() {
  constexpr std::uint64_t expected[2][RenderSettings::scales.size()][2]{
      {{0x726930ee858cde65, 0x726930ee858cde65},
       {0xee957ab37d1b836d, 0x396872ff7fa50b0d},
       {0x07d7162775af1fa7, 0x45ae2bb8eab9134b},
       {0x22e52e5ee32ea760, 0x24d63fde706ba00e}},
      {{0x378875e83ca2fea5, 0x378875e83ca2fea5},
       {0x166c7191c895a685, 0xbca0542d296b4c1d},
       {0x5bbe22d3643b491d, 0xed89297a3fd7ca2d},
       {0xe6df7e2f38e3a766, 0x684db2683b39d44a}}};
  for (const auto pal : {false, true}) {
    const unsigned height = pal ? 288 : 240;
    auto registers = base_video(2);
    registers[0] |= 0x40;
    registers[6] = pal ? 624 : 524;
    registers[10] = pal ? (44u << 16) | 620 : (34u << 16) | 514;
    for (std::size_t index = 0; index < RenderSettings::scales.size(); ++index) {
      const auto scale = RenderSettings::scales[index];
      GpuFixture fixture;
      recreate(fixture, {.scale = scale, .supersampling = scale > 1});
      fixture.program(registers);
      for (unsigned y = 0; y < 512; ++y)
        for (unsigned x = 0; x < 64; x += 2)
          fixture.put(0x100000 + (y * 64 + x) * 2,
                      (std::uint32_t(pixel16(x, y)) << 16) | pixel16(x + 1, y));
      const auto guest = CoreState::capture(fixture.console);
      for (const auto field : {false, true}) {
        const auto frame = fixture.renderer->frame(field);
        std::cout << (pal ? "PAL" : "NTSC") << ' ' << scale << "x field " << field << ": "
                  << frame.width << 'x' << frame.height << '\n';
        dimensions(frame, 1, height * (scale > 1 ? 2 : 1));
        test::equal(fingerprint(frame.rgba), expected[pal][index][field]);
        test::equal(CoreState::capture(fixture.console) == guest, true);
      }
    }
  }
}

void invalid_settings() {
  GpuFixture fixture;
  const auto before = CoreState::capture(fixture.console);
  for (const auto settings : {RenderSettings{.scale = 3}, RenderSettings{.supersampling = true},
                              RenderSettings{.scale = 2, .supersampling = true, .weave = true}}) {
    bool rejected = false;
    try {
      HardwareRenderer invalid(fixture.console.ram(), settings);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    test::equal(rejected, true);
    test::equal(CoreState::capture(fixture.console) == before, true);
  }
}

} // namespace

int main() {
  try {
    GpuFixture available;
  } catch (const std::runtime_error &error) {
    std::cerr << "Hardware rendering unavailable: " << error.what() << '\n';
    return 77;
  }
  modes();
  processing();
  textured_readback();
  interlaced_supersampling();
  invalid_settings();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
