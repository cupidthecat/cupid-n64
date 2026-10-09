#include "../support/test.hpp"
#include "core/system/console.hpp"
#include "renderer/vulkan/renderer.hpp"
#include <algorithm>

using namespace cupid::n64;

namespace test {
void gpu_cache_visibility_tests(Console &console, HardwareRenderer &renderer);
void gpu_depth_cache_tests(Console &console, HardwareRenderer &renderer);
void gpu_cache_overlap_tests(Console &console, HardwareRenderer &renderer);
void gpu_command_replay_tests();
void gpu_crash_reset_tests();
void gpu_video_replay_tests();
} // namespace test

namespace {

VideoFrame coverage_scanout(HardwareRenderer &renderer) {
  const std::uint32_t registers[][2] = {{0, 0x002},
                                        {1, 0x180000},
                                        {2, 64},
                                        {6, 525},
                                        {7, 3093},
                                        {8, 0x0c150c15},
                                        {9, (108u << 16) | 748},
                                        {10, (34u << 16) | 514},
                                        {12, 102},
                                        {13, 273}};
  for (const auto &reg : registers)
    renderer.write_video(reg[0], reg[1]);
  auto frame = renderer.frame(false);
  test::equal(frame.width, 640);
  test::equal(frame.height, 240);
  constexpr std::uint8_t colors[][3] = {{16, 213, 197}, {80, 213, 134}, {143, 213, 70},
                                        {207, 213, 7},  {15, 213, 199}, {79, 213, 136},
                                        {142, 213, 72}, {206, 213, 9}};
  if (frame.width == 640 && frame.height == 240) {
    for (unsigned sample = 0; sample < std::size(colors); ++sample) {
      const auto offset = (220 * frame.width + 20 + sample * 80) * 4;
      for (unsigned component = 0; component < 3; ++component)
        test::equal(frame.rgba[offset + component], colors[sample][component]);
    }
  }
  return frame;
}

} // namespace

int main() {
  Console console({.random_seed = 0});
  console.write(0x04700008, 4, 0);
  console.write(0x0470000c, 4, 0x14);
  console.write(0x04300000, 4, 0x10f);
  console.write(0x03f80008, 4, 0x00080008);
  for (unsigned chip = 0; chip < 4; ++chip) {
    console.write(0x03f0000c, 4, 0x02000000);
    console.write(0x03f00004, 4, (chip + 4) * 2 << 26);
  }
  for (unsigned chip = 0; chip < 4; ++chip)
    console.write(0x03f00004 + (chip + 4) * 0x800, 4, chip * 2 << 26);
  for (unsigned n = 0; n < 64 * 64; n += 2) {
    const auto pixel = [](unsigned n) {
      const auto x = n % 64, y = n / 64;
      return std::uint16_t(((x & 31) << 11) | ((y & 31) << 6) | (((x ^ y) & 31) << 1) | 1);
    };
    console.ram().write(0x180000 + n * 2, 4, (std::uint32_t(pixel(n)) << 16) | pixel(n + 1), true);
  }
  std::unique_ptr<HardwareRenderer> renderer;
  try {
    renderer = std::make_unique<HardwareRenderer>(console.ram());
  } catch (const std::runtime_error &error) {
    std::cout << error.what() << '\n';
    return 77;
  }
  test::equal(std::all_of(console.ram().hidden().begin(), console.ram().hidden().end(),
                          [](auto value) { return value == 3; }),
              true);
  test::equal(console.ram().read(0, 8, true), 0x0000000f0000000full);
  const auto cold_coverage = coverage_scanout(*renderer);
  console.ram().write(0, 4, 0x12345678);
  test::equal(console.ram().read(0, 4, true), 0);
  renderer.reset();
  test::equal(console.ram().read(0, 4, true), 0);
  console.power(true);
  renderer = std::make_unique<HardwareRenderer>(console.ram());
  test::equal(console.ram().read(0, 4), 0x12345678);
  test::equal(console.ram().read(0, 4, true), 15);
  const auto blank = renderer->frame(false);
  test::equal(blank.width, 1);
  test::equal(blank.height, 1);
  test::equal(blank.rgba.size(), 4);
  if (blank.rgba.size() == 4)
    for (unsigned n = 0; n < 4; ++n)
      test::equal(blank.rgba[n], n == 3 ? 255 : 0);
  const auto warm_coverage = coverage_scanout(*renderer);
  test::equal(cold_coverage.rgba == warm_coverage.rgba, true);
  console.display().connect([&](std::span<const std::uint32_t> words) { renderer->submit(words); },
                            [&] { renderer->synchronize(); });
  constexpr std::uint64_t commands[] = {0x3f10003f00100000, 0x2d00000000100100, 0x2f30000000000000,
                                        0x37000000f801f801, 0x360fc0fc00000000, 0x2900000000000000};
  for (unsigned n = 0; n < std::size(commands); ++n)
    console.ram().write(0x2000 + n * 8, 8, commands[n]);
  console.display().write_word(0, 0x2000);
  console.display().write_word(4, 0x2000 + sizeof(commands));
  test::equal(console.interrupts().read_word(8) & 32, 32);
  test::equal(renderer->crashed(), false);
  for (unsigned n = 0; n < 64 * 64 / 2; ++n)
    test::equal(console.ram().read(0x100000 + n * 4, 4), 0xf801f801);
  test::equal(console.ram().hidden()[0x80000] & 3, 3);
  renderer->write_video(0, 0x302);
  renderer->write_video(1, 0x100000);
  renderer->write_video(2, 64);
  renderer->write_video(6, 525);
  renderer->write_video(7, 3093);
  renderer->write_video(8, 0x0c150c15);
  renderer->write_video(9, (108u << 16) | 748);
  renderer->write_video(10, (34u << 16) | 514);
  renderer->write_video(12, 102);
  renderer->write_video(13, 273);
  const auto frame = renderer->frame(false);
  test::equal(frame.width > 0 && frame.height > 0, true);
  test::equal(frame.rgba.size(), std::size_t(frame.width) * frame.height * 4);
  if (!frame.rgba.empty()) {
    const auto center = (std::size_t(frame.height / 2) * frame.width + frame.width / 2) * 4;
    test::equal(frame.rgba[center] > 200, true);
    test::equal(frame.rgba[center + 1], 0);
    test::equal(frame.rgba[center + 2], 0);
  }
  test::gpu_cache_visibility_tests(console, *renderer);
  test::gpu_depth_cache_tests(console, *renderer);
  test::gpu_cache_overlap_tests(console, *renderer);
  auto &cpu = console.cpu();
  cpu.power();
  cpu.write_control(Status, 0x30000000);
  cpu.state().gpr[5] = 0x30000000;
  console.ram().write(0x100000, 4, test::i(9, 0, 4, 1));
  console.ram().write(0x100004, 4, test::c(4, 5, Status));
  cpu.set_pc(0xffffffff80100000);
  test::equal(cpu.run_block(cpu.state().clocks), true);
  test::equal(cpu.state().gpr[4], 1);
  auto *tracker = console.instruction_tracker();
  test::equal(tracker != nullptr, true);
  if (!tracker)
    return 1;
  for (unsigned pass = 0; pass < 3; ++pass) {
    const auto generation = tracker->generation(0x100000);
    const auto value = pass == 2 ? 13u : 9u;
    renderer->begin_frame(false);
    const std::uint32_t packets[][2] = {
        {0x3f18003f, 0x00100000},
        {0x2d000000, 0x00100100},
        {0x2f300000, 0},
        {0x37000000, test::i(9, 0, 4, static_cast<std::uint16_t>(value))},
        {0x36000000, 0}};
    for (const auto &packet : packets)
      renderer->submit(packet);
    test::equal(console.instruction_tracker() == tracker, true);
    test::equal(tracker->fully_tracked(), true);
    if (pass != 2) {
      const auto queued_frame = renderer->read_frame();
      test::equal(queued_frame.rgba.empty(), false);
      test::equal(console.instruction_tracker() == tracker, true);
      test::equal(tracker->fully_tracked(), true);
    }
    if (pass == 2)
      renderer.reset();
    else
      renderer->synchronize();
    test::equal(console.instruction_tracker() == tracker, true);
    test::equal(tracker->fully_tracked(), true);
    test::equal(tracker->generation(0x100000), generation);
    cpu.state().gpr[30] = 0xffffffff80100000;
    cpu.execute(test::i(47, 30, 16, 0));
    cpu.set_pc(0xffffffff80100000);
    test::equal(cpu.run_block(cpu.state().clocks), true);
    test::equal(cpu.state().gpr[4], 1);
    cpu.set_pc(0xffffffff80100000);
    cpu.step();
    test::equal(cpu.state().gpr[4], value);
  }
  if (!test::failures)
    test::gpu_command_replay_tests();
  if (!test::failures)
    test::gpu_crash_reset_tests();
  if (!test::failures)
    test::gpu_video_replay_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
