#pragma once

#include "desktop/audio/levels.hpp"
#include "desktop/audio/resampler.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cupid::desktop {

class AudioOutput;

class Audio {
public:
  Audio();
  ~Audio();
  Audio(const Audio &) = delete;
  Audio &operator=(const Audio &) = delete;
  void sample(n64::StereoSample value);
  void frequency(unsigned value);
  void clear();
  void play_saved_output();
  void wait_interrupt(std::function<bool()> predicate);
  struct Settings {
    AudioLevels levels;
    unsigned latency = 40;
    std::wstring device;
    bool blocking = true;
  };
  struct Device {
    std::wstring identity, name;
  };
  std::vector<Device> devices() const;
  void configure(Settings settings);
  bool recover();
  // Probe on the main thread; recovery still requires a joined producer.
  void probe();
  bool recovery_pending() const;
  void measure(bool enabled) {
    measure_ = enabled;
    generated_samples_ = 0;
    wait_time_ = {};
  }
  std::uint64_t generated_samples() const {
    return generated_samples_;
  }
  std::chrono::nanoseconds wait_time() const {
    return wait_time_;
  }
  const Settings &settings() const {
    return settings_;
  }
  const std::wstring &device_name() const;
  bool using_fallback() const;
  bool available() const;
  std::wstring error() const;
  bool muted = false;
  bool suspended = false;

private:
  friend class SessionState;
  struct Buffer {
    std::array<short, 960> samples{};
    std::array<short, 960> rendered{};
  };
  void queue(unsigned samples);
  bool wait();
  std::vector<Buffer> buffers_{32};
  unsigned buffer_ = 0, offset_ = 0;
  AudioResampler resampler_;
  Settings settings_;
  std::unique_ptr<AudioOutput> output_;
  bool measure_ = false;
  std::uint64_t generated_samples_ = 0;
  std::chrono::nanoseconds wait_time_{};
};

} // namespace cupid::desktop
