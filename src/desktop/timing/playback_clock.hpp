#pragma once

#include <chrono>
#include <cstdint>

namespace cupid::desktop {

class PlaybackClock {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  void reset(std::uint64_t clocks, TimePoint now);
  std::chrono::milliseconds delay(std::uint64_t clocks, TimePoint now);

private:
  std::uint64_t origin_clocks_ = 0;
  TimePoint origin_time_{};
};

} // namespace cupid::desktop
