#pragma once

#include "core/devices/audio/audio_interface.hpp"
#include <array>
#include <string>
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
  bool available() const {
    return device_ != nullptr && error_ == MMSYSERR_NOERROR;
  }
  std::wstring error() const;
  bool muted = false;

private:
  struct Buffer {
    WAVEHDR header{};
    std::array<short, 960> samples{};
  };
  HWAVEOUT device_ = nullptr;
  MMRESULT error_ = MMSYSERR_NOERROR;
  std::array<Buffer, 8> buffers_{};
  unsigned buffer_ = 0, offset_ = 0;
  double position_ = 0, step_ = 44100.0 / 48000;
  n64::StereoSample previous_{};
};

} // namespace cupid::desktop
