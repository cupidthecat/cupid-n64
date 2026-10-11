#include "desktop/video/bitmap.hpp"
#include "desktop/video/viewport.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks = 0, failures = 0;
void check(bool condition) {
  ++checks;
  failures += !condition;
}
} // namespace

int main() {
  using namespace cupid::desktop;
  const auto rectangle = [](Viewport actual, unsigned x, unsigned y, unsigned w, unsigned h) {
    check(actual.x == x && actual.y == y && actual.width == w && actual.height == h);
  };
  rectangle(image_viewport(640, 240, 960, 720, DisplayScale::Fit, DisplayAspect::Standard), 0, 0,
            960, 720);
  rectangle(image_viewport(640, 240, 960, 720, DisplayScale::Fit, DisplayAspect::Pixels), 0, 180,
            960, 360);
  rectangle(image_viewport(320, 240, 1000, 720, DisplayScale::Integer, DisplayAspect::Standard), 20,
            0, 960, 720);
  rectangle(image_viewport(640, 240, 1280, 960, DisplayScale::Integer, DisplayAspect::Pixels), 0,
            240, 1280, 480);
  rectangle(image_viewport(640, 240, 1280, 960, DisplayScale::Integer, DisplayAspect::Standard), 0,
            0, 1280, 960);
  rectangle(image_viewport(320, 240, 160, 120, DisplayScale::Integer, DisplayAspect::Standard), 0,
            0, 160, 120);
  rectangle(image_viewport(320, 240, 700, 300, DisplayScale::Stretch, DisplayAspect::Standard), 0,
            0, 700, 300);
  rectangle(image_viewport(320, 240, 300, 600, DisplayScale::Fit, DisplayAspect::Standard), 0, 187,
            300, 225);
  rectangle(image_viewport(0, 240, 960, 720, DisplayScale::Fit, DisplayAspect::Standard), 0, 0, 0,
            0);
  rectangle(image_viewport(320, 240, 0, 720, DisplayScale::Integer, DisplayAspect::Standard), 0, 0,
            0, 0);
  rectangle(image_viewport(320, 240, 960, 0, DisplayScale::Stretch, DisplayAspect::Pixels), 0, 0, 0,
            0);
  const auto wide = image_viewport(std::numeric_limits<unsigned>::max(), 240, 960, 720,
                                   DisplayScale::Integer, DisplayAspect::Standard);
  check(wide.width == 960 && wide.height == 720);
  const std::array<std::uint8_t, 16> pixels{0,   0, 255, 255, 0,   255, 0,   255,
                                            255, 0, 0,   255, 255, 255, 255, 0};
  const auto image = bitmap_image(2, 2, pixels);
  const auto word = [&](unsigned offset) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte)
      value |= std::uint32_t(image[offset + byte]) << (byte * 8);
    return value;
  };
  check(image[0] == 'B' && image[1] == 'M' && word(2) == image.size());
  check(word(10) == 54 && word(14) == 40 && word(18) == 2 && word(22) == 2);
  check(image[26] == 1 && image[28] == 24 && word(34) == 16);
  check(image[54] == 255 && image[55] == 0 && image[56] == 0);
  check(image[57] == 255 && image[58] == 255 && image[59] == 255);
  check(image[60] == 0 && image[61] == 0 && image[62] == 0 && image[63] == 0);
  check(image[64] == 255 && image[65] == 0 && image[66] == 255 && image[67] == 0);
  check(image[68] == 0 && image[69] == 0);
  for (const auto size :
       {std::array{0u, 2u}, std::array{2u, 0u}, std::array{2u, 3u}, std::array{32769u, 2u}}) {
    bool rejected = false;
    try {
      bitmap_image(size[0], size[1], pixels);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected);
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
