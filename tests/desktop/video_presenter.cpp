#include "../support/test.hpp"
#include "desktop/windows/video/presenter.hpp"
#include <array>

using namespace cupid::desktop;

int main() {
  const auto parent = CreateWindowW(L"STATIC", L"video test", WS_OVERLAPPEDWINDOW, 0, 0, 120, 120,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  test::equal(parent != nullptr, true);
  if (!parent)
    return 1;
  {
    VideoPresenter video;
    test::equal(video.initialize(parent, true), true);
    const std::array<std::uint8_t, 16> image{0,   0, 255, 255, 0,   255, 0,   255,
                                             255, 0, 0,   255, 255, 255, 255, 255};
    test::equal(video.present(image, 2, 2, 64, 48, DisplayScale::Stretch, DisplayAspect::Standard),
                true);
    auto pixels = video.capture();
    test::equal(pixels.size(), 64 * 48 * 4);
    const auto check = [&](unsigned width, unsigned x, unsigned y,
                           std::array<std::uint8_t, 4> expected) {
      if (pixels.size() < (std::size_t(y) * width + x + 1) * 4) {
        test::equal(false, true);
        return;
      }
      for (unsigned channel = 0; channel < 4; ++channel)
        test::equal(pixels[(std::size_t(y) * width + x) * 4 + channel], expected[channel]);
    };
    check(64, 8, 8, {0, 0, 255, 255});
    check(64, 56, 8, {0, 255, 0, 255});
    check(64, 8, 40, {255, 0, 0, 255});
    check(64, 56, 40, {255, 255, 255, 255});
    test::equal(video.present(image, 2, 2, 80, 80, DisplayScale::Fit, DisplayAspect::Standard),
                true);
    pixels = video.capture();
    test::equal(pixels.size(), 80 * 80 * 4);
    check(80, 40, 0, {0, 0, 0, 255});
    check(80, 40, 79, {0, 0, 0, 255});
    check(80, 8, 20, {0, 0, 255, 255});
    check(80, 72, 60, {255, 255, 255, 255});
    video.hide();
    test::equal(video.available(), true);
    test::equal(video.present(image, 2, 2, 64, 48, DisplayScale::Stretch, DisplayAspect::Pixels),
                true);
    pixels = video.capture();
    check(64, 56, 8, {0, 255, 0, 255});
  }
  DestroyWindow(parent);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
