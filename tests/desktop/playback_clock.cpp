#include "desktop/timing/playback_clock.hpp"
#include "../support/test.hpp"
#include "core/timing/frequencies.hpp"

using namespace cupid::desktop;
using namespace std::chrono_literals;

int main() {
  PlaybackClock playback;
  const PlaybackClock::TimePoint start{};
  constexpr auto frequency = cupid::n64::clock_frequency;
  playback.reset(0, start);
  test::equal(playback.delay(0, start).count(), 0);
  test::equal(playback.delay(1, start).count(), 1);
  test::equal(playback.delay(frequency, start).count(), 20);
  test::equal(playback.delay(frequency, start + 999ms).count(), 1);
  test::equal(playback.delay(frequency, start + 999500us).count(), 1);
  test::equal(playback.delay(frequency, start + 1s).count(), 0);

  for (auto wake : {1ms, 2ms, 5ms, 10ms, 17ms}) {
    playback.reset(0, start);
    std::uint64_t clocks = 0;
    unsigned fields = 0;
    for (auto wall = 0ms; wall < 5s; wall += wake) {
      if (playback.delay(clocks, start + wall) == 0ms) {
        clocks += frequency / 60;
        ++fields;
      }
      const auto maximum = std::uint64_t(wall.count()) * frequency / 1000 + frequency / 60;
      test::equal(clocks <= maximum, true);
    }
    test::equal(fields <= 300, true);
    if (wake <= 10ms)
      test::equal(fields, 300);
  }

  playback.reset(frequency * 3600ull, start + 1h);
  test::equal(playback.delay(frequency * 3600ull, start + 1h).count(), 0);
  test::equal(playback.delay(frequency * 3600ull + frequency / 60, start + 1h).count(), 17);
  playback.reset(frequency * 3600ull, start + 2h);
  test::equal(playback.delay(frequency * 3600ull + frequency / 60, start + 2h).count(), 17);

  playback.reset(0, start);
  test::equal(playback.delay(frequency, start + 1100ms).count(), 0);
  test::equal(playback.delay(frequency + frequency / 60, start + 1100ms).count(), 0);
  test::equal(playback.delay(frequency, start + 1101ms).count(), 0);
  test::equal(playback.delay(frequency + frequency / 60, start + 1101ms).count(), 17);

  playback.reset(frequency, start);
  test::equal(playback.delay(0, start + 1s).count(), 0);
  test::equal(playback.delay(frequency / 60, start + 1s).count(), 17);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
