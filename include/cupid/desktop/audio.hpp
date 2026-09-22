#pragma once

#include "cupid/types.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace cupid::desktop {

struct AudioStatus {
    bool open{};
    bool running{};
    bool muted{};
    bool available{};
    std::size_t queued_frames{};
    std::size_t dropped_frames{};
    std::size_t underruns{};
    float gain{1.0F};
    std::string error;
};

class AudioOutput {
  public:
    static constexpr unsigned sample_rate = 48000;
    static constexpr unsigned channels = 2;
    static constexpr std::size_t prefill_frames = 960;
    static constexpr std::size_t max_queue_frames = 4800;

    AudioOutput();
    ~AudioOutput();

    AudioOutput(AudioOutput&&) noexcept;
    AudioOutput& operator=(AudioOutput&&) noexcept;

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    [[nodiscard]] bool open(std::string& error);
    [[nodiscard]] bool submit(std::span<const s16> samples, std::string& error);
    [[nodiscard]] bool set_running(bool running, std::string& error);
    [[nodiscard]] bool set_volume(float gain, bool mute, std::string& error);
    void clear();
    void close();
    [[nodiscard]] AudioStatus status() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cupid::desktop
