#include "desktop/timing/playback_clock.hpp"
#include "core/timing/frequencies.hpp"
#include <algorithm>

namespace cupid::desktop {

void PlaybackClock::reset(std::uint64_t clocks, TimePoint now) {
  origin_clocks_ = clocks;
  origin_time_ = now;
}

std::chrono::milliseconds PlaybackClock::delay(std::uint64_t clocks, TimePoint now) {
  if (clocks < origin_clocks_) {
    reset(clocks, now);
    return {};
  }
  const auto elapsed =
      std::chrono::duration<double>((clocks - origin_clocks_) / double(n64::clock_frequency));
  const auto deadline = origin_time_ + std::chrono::duration_cast<Clock::duration>(elapsed);
  if (deadline > now)
    return std::min(std::chrono::ceil<std::chrono::milliseconds>(deadline - now),
                    std::chrono::milliseconds(20));
  if (now - deadline > std::chrono::milliseconds(100))
    reset(clocks, now);
  return {};
}

} // namespace cupid::desktop
