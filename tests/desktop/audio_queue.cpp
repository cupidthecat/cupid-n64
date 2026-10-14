#include "../support/test.hpp"
#include "desktop/audio/frame_queue.hpp"
#include <deque>
#include <random>

int main() {
  using namespace cupid::desktop;
  AudioFrameQueue queue(7);
  std::deque<std::array<float, 2>> expected;
  std::mt19937 random(0x5127);
  for (unsigned interval = 0; interval < 4096; ++interval) {
    const auto frames = random() % 12;
    std::vector<float> input;
    for (unsigned frame = 0; frame < frames; ++frame) {
      const auto left = static_cast<float>(interval * 12 + frame), right = -left - 0.25f;
      input.push_back(left);
      input.push_back(right);
      expected.push_back({left, right});
      if (expected.size() > 7)
        expected.pop_front();
    }
    queue.append(input);
    test::equal(queue.size(), expected.size());
    const auto drain = random() % (expected.size() + 1);
    std::vector<float> output(drain * 2);
    queue.read(output);
    for (std::size_t frame = 0; frame < drain; ++frame) {
      test::equal(output[frame * 2] == expected.front()[0], true);
      test::equal(output[frame * 2 + 1] == expected.front()[1], true);
      expected.pop_front();
    }
    if (interval % 5 == 0) {
      queue.discard(100);
      expected.clear();
    }
    test::equal(queue.size(), expected.size());
  }
  queue.clear();
  test::equal(queue.size(), std::size_t(0));
  const auto rejects = [](auto action) {
    try {
      action();
      return false;
    } catch (const std::invalid_argument &) {
      return true;
    }
  };
  test::equal(rejects([&] { queue.append(std::array<float, 1>{}); }), true);
  std::array<float, 2> missing{};
  test::equal(rejects([&] { queue.read(missing); }), true);
  test::equal(rejects([] { AudioFrameQueue empty(0); }), true);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
