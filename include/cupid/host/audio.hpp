#pragma once

#include "cupid/rcp/audio.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace cupid::host {

inline constexpr unsigned playback_frequency = 48000;

class AudioResampler {
  public:
    void reset();
    void append(const AudioSample& sample, std::vector<s16>& interleaved);
    [[nodiscard]] u64 discontinuities() const {
        return discontinuities_;
    }

  private:
    AudioSample previous_{};
    u64 fraction_{};
    u64 discontinuities_{};
    bool started_{};
};

class AudioBuffer {
  public:
    static constexpr std::size_t capacity_frames = playback_frequency / 10;

    void clear();
    void append(std::span<const s16> interleaved);
    [[nodiscard]] std::vector<s16> take(std::size_t max_frames = capacity_frames);
    [[nodiscard]] std::size_t frames() const {
        return size_;
    }
    [[nodiscard]] u64 dropped_frames() const {
        return dropped_;
    }

  private:
    std::array<std::array<s16, 2>, capacity_frames> samples_{};
    std::size_t first_{};
    std::size_t size_{};
    u64 dropped_{};
};

} // namespace cupid::host
