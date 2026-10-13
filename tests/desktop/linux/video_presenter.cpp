#include "../../support/test.hpp"
#include "desktop/linux/video/presenter.hpp"
#include "renderer/video/readback.hpp"
#include <X11/Xlib.h>
#include <array>
#include <epoxy/glx.h>
#include <future>
#include <thread>
#include <utility>

using namespace cupid::desktop;

int main() {
  auto display = XOpenDisplay(nullptr);
  if (!display)
    return 1;
  const auto parent =
      XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 128, 128, 0, 0, 0);
  XMapWindow(display, parent);
  XSync(display, False);
  linux_host::VideoPresenter video;
  test::equal(video.initialize(parent), true);
  if (!video.available()) {
    std::cerr << video.error() << '\n';
    XDestroyWindow(display, parent);
    XCloseDisplay(display);
    return 1;
  }
  VideoSurface surface{64, 48, DisplayScale::Stretch, DisplayAspect::Pixels, false};
  video.prepare(surface);
  video.show(true);
  const auto ui = std::this_thread::get_id();
  std::promise<void> entered, release;
  auto released = release.get_future().share();
  bool held = true, rendered = true, restored = true;
  std::vector<unsigned> fields;
  std::vector<std::thread::id> threads;
  std::vector<std::uint8_t> captured;
  std::vector<std::uint8_t> captured_first;
  {
    cupid::n64::FrameReadback readback([] {
      return cupid::n64::VideoFrame{
          2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}};
    });
    readback.connect_present([&](unsigned field, const auto &frame) {
      fields.push_back(field);
      threads.push_back(std::this_thread::get_id());
      if (std::exchange(held, false)) {
        entered.set_value();
        if (released.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
          throw std::runtime_error("The held display image was not released.");
      }
      restored &= glXGetCurrentContext() == nullptr;
      auto pixels = frame.rgba;
      for (unsigned offset = 0; offset < pixels.size(); offset += 4)
        std::swap(pixels[offset], pixels[offset + 2]);
      rendered &= video.render(pixels, frame.width, frame.height, surface);
      captured = video.capture();
      if (field == 17)
        captured_first = captured;
      restored &= glXGetCurrentContext() == nullptr;
    });
    readback.start(17);
    test::equal(entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                true);
    auto copy = readback.take();
    test::equal(copy.rgba[0], 255);
    copy.rgba[0] = 0;
    auto wrong_thread = std::async(std::launch::async, [&] {
      try {
        video.prepare(surface);
        return false;
      } catch (const std::logic_error &) {
        return true;
      }
    });
    test::equal(wrong_thread.get(), true);
    auto next = std::async(std::launch::async, [&] { readback.start(18); });
    test::equal(next.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout, true);
    release.set_value();
    next.get();
    readback.wait_presented();
    test::equal(rendered, true);
    test::equal(restored, true);
    test::equal(fields == std::vector<unsigned>{17, 18}, true);
    test::equal(threads[0] != ui && threads[0] == threads[1], true);
    test::equal(captured.size(), 64 * 48 * 4);
    test::equal(captured_first == captured, true);
    if (captured.size() == 64 * 48 * 4) {
      for (unsigned y = 0; y < 48; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
          const auto offset = (y * 64 + x) * 4;
          const std::array<std::uint8_t, 4> expected =
              y < 24 ? (x < 32 ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                               : std::array<std::uint8_t, 4>{0, 255, 0, 255})
                     : (x < 32 ? std::array<std::uint8_t, 4>{255, 0, 0, 255}
                               : std::array<std::uint8_t, 4>{255, 255, 255, 255});
          for (unsigned channel = 0; channel < 4; ++channel)
            test::equal(captured[offset + channel], expected[channel]);
        }
      }
    }
    surface.width = 80;
    surface.height = 40;
    video.prepare(surface);
    auto resized = std::async(std::launch::async, [&] { return video.capture(); }).get();
    test::equal(resized.size(), 80 * 40 * 4);
    test::equal(resized[2], 255);
    test::equal(resized[(39 * 80) * 4], 255);
    video.show(false);
    auto hidden = std::async(std::launch::async, [&] { return video.capture(); }).get();
    test::equal(hidden == resized, true);
    readback.connect_present({});
  }
  std::async(std::launch::async, [&] { video.release_graphics(); }).get();
  test::equal(video.available(), false);
  video.close();
  XDestroyWindow(display, parent);
  XCloseDisplay(display);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
