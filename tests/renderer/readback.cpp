#include "renderer/video/readback.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

using namespace cupid::n64;
using namespace std::chrono_literals;

namespace {

void delivery() {
  std::promise<void> entered, release;
  auto started = entered.get_future();
  auto gate = release.get_future().share();
  std::atomic<unsigned> calls = 0;
  FrameReadback readback([&] {
    const auto sequence = ++calls;
    if (sequence == 1) {
      entered.set_value();
      gate.wait();
    }
    VideoFrame frame{64, 32, std::vector<std::uint8_t>(64 * 32 * 4)};
    for (std::size_t n = 0; n < frame.rgba.size(); ++n)
      frame.rgba[n] = static_cast<std::uint8_t>(sequence + n);
    return frame;
  });
  readback.start();
  test::equal(started.wait_for(2s) == std::future_status::ready, true);
  auto output = std::async(std::launch::async, [&] { return readback.take(); });
  test::equal(output.wait_for(0s) == std::future_status::timeout, true);
  release.set_value();
  test::equal(output.wait_for(2s) == std::future_status::ready, true);
  auto frame = output.get();
  for (unsigned sequence = 1; sequence <= 256; ++sequence) {
    test::equal(frame.width, 64);
    test::equal(frame.height, 32);
    test::equal(frame.rgba.size(), 64 * 32 * 4);
    for (std::size_t n = 0; n < frame.rgba.size(); ++n)
      test::equal(frame.rgba[n], static_cast<std::uint8_t>(sequence + n));
    test::equal(readback.take().rgba.empty(), true);
    if (sequence != 256) {
      readback.start();
      frame = readback.take();
    }
  }
  test::equal(calls, 256);
}

void failure() {
  FrameReadback readback([]() -> VideoFrame { throw std::runtime_error("Readback failed"); });
  readback.start();
  unsigned failures = 0;
  try {
    readback.take();
  } catch (const std::runtime_error &) {
    ++failures;
  }
  try {
    readback.wait();
  } catch (const std::runtime_error &) {
    ++failures;
  }
  try {
    readback.start();
  } catch (const std::runtime_error &) {
    ++failures;
  }
  test::equal(failures, 3);
}

void shutdown() {
  std::promise<void> entered, release, destroying;
  auto started = entered.get_future();
  auto closing = destroying.get_future();
  auto gate = release.get_future().share();
  std::atomic<bool> completed = false;
  auto readback = std::make_unique<FrameReadback>([&] {
    entered.set_value();
    gate.wait();
    completed = true;
    return VideoFrame{1, 1, {0, 0, 0, 255}};
  });
  readback->start();
  test::equal(started.wait_for(2s) == std::future_status::ready, true);
  auto destroyed = std::async(std::launch::async, [&] {
    destroying.set_value();
    readback.reset();
  });
  test::equal(closing.wait_for(2s) == std::future_status::ready, true);
  test::equal(destroyed.wait_for(0s) == std::future_status::timeout, true);
  test::equal(completed, false);
  release.set_value();
  test::equal(destroyed.wait_for(2s) == std::future_status::ready, true);
  destroyed.get();
  test::equal(completed, true);
}

} // namespace

int main() {
  delivery();
  failure();
  shutdown();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
