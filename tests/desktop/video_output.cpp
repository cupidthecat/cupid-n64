#include "../support/test.hpp"
#include "desktop/video/output.hpp"
#include <string_view>

using namespace cupid;

namespace {

void initialize(n64::Console &console) {
  console.write(0x04700008, 4, 0);
  console.write(0x0470000c, 4, 0x14);
  console.write(0x04300000, 4, 0x10f);
  console.write(0x03f80008, 4, 0x00080008);
  for (unsigned n = 0; n < 4; ++n) {
    console.ram().write_word(0x03f0000c, 0x02000000);
    console.ram().write_word(0x03f00004, ((4 + n) * 2) << 26);
  }
  for (unsigned n = 0; n < 4; ++n)
    console.ram().write_word(0x03f00004 + ((4 + n) * 2) * 0x400, n * 2 << 26);
  test::equal(console.ram().identity(), true);
}

void program(n64::Console &console) {
  auto &video = console.video();
  video.write_word(0, 2);
  video.write_word(4, 0x1000);
  video.write_word(8, 1);
  video.write_word(24, 525);
  video.write_word(28, 3092);
  video.write_word(32, 0x0c150c15);
  video.write_word(36, (108u << 16) | 128);
  video.write_word(40, (34u << 16) | 36);
}

void pixel(const n64::VideoFrame &frame, unsigned x, unsigned y, std::uint32_t expected) {
  const auto offset = (std::size_t(y) * frame.width + x) * 4;
  const auto value = (std::uint32_t(frame.rgba.at(offset)) << 24) |
                     (std::uint32_t(frame.rgba.at(offset + 1)) << 16) |
                     (std::uint32_t(frame.rgba.at(offset + 2)) << 8) | frame.rgba.at(offset + 3);
  test::equal(value, expected);
}

} // namespace

int main(int argc, char **argv) {
  const bool automatic = argc == 2 && std::string_view(argv[1]) == "--unavailable";
  n64::ConsoleConfig config;
  config.random_seed = 0;
  n64::Console console(config);
  initialize(console);
  program(console);
  console.ram().write(0x1000, 4, 0xf80007c0);
  {
    desktop::VideoOutput output(console, automatic);
    test::equal(output.hardware_rendering(), false);
    test::equal(output.requires_hardware(), false);
    test::equal(output.crashed(), false);
    const auto clocks = console.video().clocks();
    const auto fraction = console.video().fraction();
    const auto irq = console.interrupts().read_word(8);
    output.begin_frame(console.video().field());
    console.ram().write(0x1000, 4, 0x07c0ffff);
    const auto frame = output.read_frame();
    const auto consumed = output.read_frame();
    test::equal(consumed.width, 0);
    test::equal(consumed.height, 0);
    test::equal(consumed.rgba.size(), 0);
    test::equal(frame.width, 640);
    test::equal(frame.height, 480);
    test::equal(frame.rgba.size(), 640 * 480 * 4);
    pixel(frame, 7, 0, 0x000000ff);
    pixel(frame, 8, 0, 0xff0000ff);
    pixel(frame, 12, 1, 0xff0000ff);
    pixel(frame, 13, 1, 0x000000ff);
    pixel(frame, 8, 2, 0x000000ff);
    test::equal(console.video().clocks(), clocks);
    test::equal(console.video().fraction(), fraction);
    test::equal(console.video().frames(), 0);
    test::equal(console.cpu().state().clocks, 0);
    test::equal(console.interrupts().read_word(8), irq);
    output.begin_frame(console.video().field());
    pixel(output.read_frame(), 8, 0, 0x00ff00ff);
    console.ram().write(0x2000, 4, 0x37000000);
    console.ram().write(0x2004, 4, 0x12345678);
    console.display().write_word(0, 0x2000);
    console.display().write_word(4, 0x2008);
    test::equal(output.requires_hardware(), false);
    console.interrupts().write_word(0, 0x800);
    console.ram().write(0x2008, 4, 0x29000000);
    console.ram().write(0x200c, 4, 0);
    console.display().write_word(4, 0x2010);
    test::equal(output.requires_hardware(), false);
    test::equal(console.interrupts().read_word(8) & 32, 32);
    console.ram().write(0x2010, 4, 0x36000000);
    console.ram().write(0x2014, 4, 0);
    console.display().write_word(4, 0x2018);
    test::equal(output.requires_hardware(), true);
    test::equal(output.crashed(), false);
  }
  console.power(true);
  program(console);
  {
    desktop::VideoOutput output(console, false);
    test::equal(output.requires_hardware(), false);
    output.begin_frame(console.video().field());
    pixel(output.read_frame(), 8, 0, 0x00ff00ff);
  }
  console.power();
  initialize(console);
  program(console);
  {
    desktop::VideoOutput output(console, false);
    output.begin_frame(console.video().field());
    pixel(output.read_frame(), 8, 0, 0x000000ff);
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
