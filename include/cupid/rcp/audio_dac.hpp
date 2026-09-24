#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

class AudioDac {
  public:
    void reset(u32 video_frequency);
    void write(u32 stereo);
    // Durations use the AI oscillator's common 44100 * video_frequency timebase.
    void advance_idle(u64 clocks);
    [[nodiscard]] std::array<s16, 2> sample() const;

  private:
    std::array<s16, 2> held_{};
    u64 idle_clocks_{};
    u64 clock_frequency_{1};
};

} // namespace cupid
