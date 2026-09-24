#include "cupid/rcp/audio_dac.hpp"

#include <algorithm>
#include <cmath>

namespace cupid {

void AudioDac::reset(u32 video_frequency) {
    held_ = {};
    idle_clocks_ = 0;
    clock_frequency_ = 44100ULL * video_frequency;
}

void AudioDac::write(u32 stereo) {
    held_ = {static_cast<s16>(stereo >> 16U), static_cast<s16>(stereo)};
    idle_clocks_ = 0;
}

void AudioDac::advance_idle(u64 clocks) {
    if (held_[0] == 0 && held_[1] == 0)
        return;
    // After 50 ms even a full-scale 3 ms discharge is below the analog cutoff.
    const u64 limit = clock_frequency_ / 20U;
    idle_clocks_ += std::min(clocks, limit - idle_clocks_);
    if (idle_clocks_ == limit)
        held_ = {};
}

std::array<s16, 2> AudioDac::sample() const {
    if (idle_clocks_ == 0 || (held_[0] == 0 && held_[1] == 0))
        return held_;
    const double factor =
        std::exp(-static_cast<double>(idle_clocks_) / (static_cast<double>(clock_frequency_) * 0.003));
    // Quantize only at output. Repeated observations must not round the held charge.
    return {static_cast<s16>(held_[0] * factor), static_cast<s16>(held_[1] * factor)};
}

} // namespace cupid
