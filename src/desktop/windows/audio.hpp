#pragma once

#include "desktop/audio/resampler.hpp"
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
  HANDLE completed_ = nullptr;
  MMRESULT error_ = MMSYSERR_NOERROR;
  std::array<Buffer, 8> buffers_{};
  unsigned buffer_ = 0, offset_ = 0;
  AudioResampler resampler_;
};

} // namespace cupid::desktop
