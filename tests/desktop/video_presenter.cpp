#include "../support/test.hpp"
#include "desktop/windows/video/opengl/functions.hpp"
#include "desktop/windows/video/presenter.hpp"
#include <array>

using namespace cupid::desktop;

int main(int argc, char **) {
  const bool software = argc == 1;
  test::equal(opengl::valid(nullptr), false);
  for (auto address : {std::intptr_t(1), std::intptr_t(2), std::intptr_t(3), std::intptr_t(-1)})
    test::equal(opengl::valid(reinterpret_cast<PROC>(address)), false);
  const auto parent = CreateWindowW(L"STATIC", L"video test", WS_OVERLAPPEDWINDOW, 0, 0, 120, 120,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  test::equal(parent != nullptr, true);
  if (!parent)
    return 1;
  const auto external_display = GetDC(parent);
  PIXELFORMATDESCRIPTOR format{};
  format.nSize = sizeof(format);
  format.nVersion = 1;
  format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  format.iPixelType = PFD_TYPE_RGBA;
  format.cColorBits = 32;
  const auto pixel_format = ChoosePixelFormat(external_display, &format);
  test::equal(SetPixelFormat(external_display, pixel_format, &format) != FALSE, true);
  const auto external_context = wglCreateContext(external_display);
  test::equal(external_context != nullptr, true);
  test::equal(wglMakeCurrent(external_display, external_context) != FALSE, true);
  {
    VideoPresenter video;
    const bool initialized = video.initialize(parent, software);
    if (!initialized && !software) {
      std::cerr << video.error() << '\n';
      DestroyWindow(parent);
      return 77;
    }
    test::equal(initialized, true);
    test::equal(wglGetCurrentContext() == external_context, true);
    if (!initialized) {
      std::cerr << video.error() << '\n';
      DestroyWindow(parent);
      return 1;
    }
    const std::array<std::uint8_t, 16> image{0,   0, 255, 255, 0,   255, 0,   255,
                                             255, 0, 0,   255, 255, 255, 255, 255};
    test::equal(video.present(image, 2, 2, 64, 48, DisplayScale::Stretch, DisplayAspect::Standard),
                true);
    auto pixels = video.capture();
    test::equal(wglGetCurrentContext() == external_context, true);
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
    const std::array<std::uint8_t, 24> resized{0,   0, 255, 255, 0,   255, 0,   255,
                                               255, 0, 0,   255, 255, 255, 255, 255,
                                               0,   0, 0,   255, 0,   255, 255, 255};
    if (!software) {
      test::equal(
          video.present(resized, 3, 2, 90, 60, DisplayScale::Stretch, DisplayAspect::Pixels), true);
      pixels = video.capture();
      test::equal(pixels.size(), 90 * 60 * 4);
      check(90, 15, 15, {0, 0, 255, 255});
      check(90, 75, 15, {255, 0, 0, 255});
      check(90, 15, 45, {255, 255, 255, 255});
      check(90, 75, 45, {0, 255, 255, 255});
    }
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
  test::equal(wglGetCurrentContext() == external_context, true);
  test::equal(wglGetCurrentDC() == external_display, true);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(external_context);
  ReleaseDC(parent, external_display);
  DestroyWindow(parent);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
