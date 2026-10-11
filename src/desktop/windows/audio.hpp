#pragma once

#include "desktop/audio/levels.hpp"
#include "desktop/audio/resampler.hpp"
#include <array>
#include <string>
#include <vector>
#include <windows.h>

#include <mmsystem.h>

namespace cupid::desktop {

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
  const std::wstring &device_name() const {
    return device_name_;
  }
  bool using_fallback() const {
    return available() && !settings_.device.empty() && settings_.device != device_identity_;
  }
  bool available() const {
    return device_ != nullptr && error_ == MMSYSERR_NOERROR;
  }
  std::wstring error() const;
  bool muted = false;
  bool suspended = false;

private:
  friend class SessionState;
  struct Buffer {
    WAVEHDR header{};
    std::array<short, 960> samples{};
    std::array<short, 960> rendered{};
  };
  void open();
  void close();
  void queue(unsigned samples);
  HWAVEOUT device_ = nullptr;
  HANDLE completed_ = nullptr;
  MMRESULT error_ = MMSYSERR_NOERROR;
  std::vector<Buffer> buffers_{32};
  unsigned buffer_ = 0, offset_ = 0;
  AudioResampler resampler_;
  Settings settings_;
  std::wstring device_name_, device_identity_;
  bool preferred_present_ = false;
};

} // namespace cupid::desktop
