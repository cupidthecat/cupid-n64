#include "desktop/windows/audio.hpp"
#include <algorithm>

namespace cupid::desktop {

Audio::Audio() {
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 48000;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  error_ = waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
  if (error_ != MMSYSERR_NOERROR) {
    device_ = nullptr;
    return;
  }
  for (auto &buffer : buffers_) {
    buffer.header.lpData = reinterpret_cast<char *>(buffer.samples.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(sizeof(buffer.samples));
    error_ = waveOutPrepareHeader(device_, &buffer.header, sizeof(WAVEHDR));
    if (error_ != MMSYSERR_NOERROR) {
      for (auto &prepared : buffers_)
        if (prepared.header.dwFlags & WHDR_PREPARED)
          waveOutUnprepareHeader(device_, &prepared.header, sizeof(WAVEHDR));
      waveOutClose(device_);
      device_ = nullptr;
      break;
    }
  }
}

std::wstring Audio::error() const {
  std::array<wchar_t, MAXERRORLENGTH> text{};
  waveOutGetErrorTextW(error_, text.data(), static_cast<UINT>(text.size()));
  return text.data();
}

Audio::~Audio() {
  if (!device_)
    return;
  waveOutReset(device_);
  for (auto &buffer : buffers_)
    waveOutUnprepareHeader(device_, &buffer.header, sizeof(WAVEHDR));
  waveOutClose(device_);
}

void Audio::frequency(unsigned value) {
  step_ = std::max(1u, value) / 48000.0;
  position_ = 0;
}

void Audio::clear() {
  if (device_)
    waveOutReset(device_);
  buffer_ = offset_ = 0;
  position_ = 0;
  previous_ = {};
}

void Audio::sample(n64::StereoSample value) {
  if (!available() || muted) {
    previous_ = value;
    return;
  }
  while (position_ < 1.0) {
    auto &buffer = buffers_[buffer_];
    if (buffer.header.dwFlags & WHDR_INQUEUE) {
      position_ = 1.0;
      break;
    }
    const auto interpolate = [this](double a, double b) {
      return static_cast<short>(std::clamp(a + (b - a) * position_, -1.0, 32767.0 / 32768) * 32768);
    };
    buffer.samples[offset_++] = interpolate(previous_.left, value.left);
    buffer.samples[offset_++] = interpolate(previous_.right, value.right);
    if (offset_ == buffer.samples.size()) {
      error_ = waveOutWrite(device_, &buffer.header, sizeof(WAVEHDR));
      offset_ = 0;
      buffer_ = (buffer_ + 1) % static_cast<unsigned>(buffers_.size());
    }
    position_ += step_;
  }
  position_ -= 1.0;
  previous_ = value;
}

} // namespace cupid::desktop
