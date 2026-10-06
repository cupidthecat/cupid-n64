#include "../support/test.hpp"
#include "core/system/console.hpp"
#include "renderer/vulkan/renderer.hpp"

using namespace cupid::n64;

int main() {
  Console console;
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
  std::unique_ptr<HardwareRenderer> renderer;
  try {
    renderer = std::make_unique<HardwareRenderer>(console.ram());
  } catch (const std::runtime_error &error) {
    std::cout << error.what() << '\n';
    return 77;
  }
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
  renderer.reset();
  test::equal(console.ram().hidden()[0x80000] & 3, 3);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
