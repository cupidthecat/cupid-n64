#pragma once

#include "desktop/audio/levels.hpp"
#include "desktop/audio/resampler.hpp"
#include <array>
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
  struct Settings {
    AudioLevels levels;
    unsigned latency = 80;
    std::wstring device;
  };
  void configure(Settings settings);
  bool recover();
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
  std::vector<Buffer> buffers_{32};
  unsigned buffer_ = 0, offset_ = 0;
  AudioResampler resampler_;
  Settings settings_;
  std::unique_ptr<AudioOutput> output_;
};

} // namespace cupid::desktop
