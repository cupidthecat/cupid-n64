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

void presentation_order() {
  std::promise<void> entered, release, next_entered;
  auto presenting = entered.get_future();
  auto gate = release.get_future().share();
  auto submitting = next_entered.get_future();
  std::atomic<unsigned> reads = 0;
  std::atomic<bool> immutable = false;
  std::thread::id reader, presenter;
  std::vector<unsigned> fields;
  FrameReadback readback([&] {
    reader = std::this_thread::get_id();
    return VideoFrame{1, 1, {static_cast<std::uint8_t>(++reads), 0, 0, 255}};
  });
  readback.connect_present([&](unsigned field, const VideoFrame &frame) {
    presenter = std::this_thread::get_id();
    fields.push_back(field);
    if (field == 17) {
      entered.set_value();
      if (gate.wait_for(2s) != std::future_status::ready)
        throw std::runtime_error("The held presentation was not released.");
      immutable = frame.rgba == std::vector<std::uint8_t>{1, 0, 0, 255};
    }
  });
  readback.start(17);
  test::equal(presenting.wait_for(2s) == std::future_status::ready, true);
  readback.wait();
  auto copied = readback.take();
  test::equal(copied.rgba == std::vector<std::uint8_t>{1, 0, 0, 255}, true);
  copied.rgba[0] = 99;
  // The caller can prepare the next scanout after copying, before presentation returns.
  auto next = std::async(std::launch::async, [&] {
    next_entered.set_value();
    readback.start(18);
  });
  test::equal(submitting.wait_for(2s) == std::future_status::ready, true);
  test::equal(next.wait_for(20ms) == std::future_status::timeout, true);
  test::equal(reads.load(), 1);
  auto finished = std::async(std::launch::async, [&] { readback.wait_presented(); });
  test::equal(finished.wait_for(20ms) == std::future_status::timeout, true);
  release.set_value();
  test::equal(next.wait_for(2s) == std::future_status::ready, true);
  next.get();
  readback.wait_presented();
  finished.get();
  test::equal(immutable.load(), true);
  test::equal(fields == std::vector<unsigned>{17, 18}, true);
  test::equal(reader == presenter && reader != std::this_thread::get_id(), true);
  test::equal(readback.capture().rgba == std::vector<std::uint8_t>{2, 0, 0, 255}, true);
  readback.connect_present({});
  readback.start(19);
  readback.wait_presented();
  test::equal(reads.load(), 3);
  test::equal(fields.size(), 2);
}

void presentation_failure() {
  FrameReadback readback([] { return VideoFrame{1, 1, {1, 0, 0, 255}}; });
  readback.connect_present(
      [](unsigned, const VideoFrame &) { throw std::runtime_error("Presentation failed."); });
  readback.start(5);
  unsigned failures = 0;
  for (const auto wait : {true, false}) {
    try {
      if (wait)
        readback.wait_presented();
      else
        readback.start(6);
    } catch (const std::runtime_error &) {
      ++failures;
    }
  }
  test::equal(failures, 2);
  readback.connect_present({});
  readback.restore({1, 1, {9, 0, 0, 255}});
  test::equal(readback.take().rgba[0], 9);
  readback.start(7);
  readback.wait_presented();
  test::equal(readback.take().rgba[0], 1);
}

void presentation_shutdown() {
  std::promise<void> entered, release, destroying;
  auto presenting = entered.get_future();
  auto gate = release.get_future().share();
  auto closing = destroying.get_future();
  std::atomic<bool> completed = false;
  auto readback = std::make_unique<FrameReadback>([] { return VideoFrame{1, 1, {1, 0, 0, 255}}; });
  readback->connect_present([&](unsigned, const VideoFrame &frame) {
    entered.set_value();
    if (gate.wait_for(2s) != std::future_status::ready)
      throw std::runtime_error("The closing presentation was not released.");
    completed = frame.rgba == std::vector<std::uint8_t>{1, 0, 0, 255};
  });
  readback->start(31);
  test::equal(presenting.wait_for(2s) == std::future_status::ready, true);
  auto destroyed = std::async(std::launch::async, [&] {
    destroying.set_value();
    readback.reset();
  });
  test::equal(closing.wait_for(2s) == std::future_status::ready, true);
  test::equal(destroyed.wait_for(20ms) == std::future_status::timeout, true);
  test::equal(completed.load(), false);
  release.set_value();
  test::equal(destroyed.wait_for(2s) == std::future_status::ready, true);
  destroyed.get();
  test::equal(completed.load(), true);
}

} // namespace

int main() {
  delivery();
  failure();
  shutdown();
  presentation_order();
  presentation_failure();
  presentation_shutdown();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
