#include "desktop/windows/audio.hpp"
#include "desktop/windows/audio_device.hpp"
#include <algorithm>

namespace cupid::desktop {

Audio::Audio() {
  completed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!completed_) {
    error_ = MMSYSERR_NOMEM;
    return;
  }
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = AudioResampler::output_frequency;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  error_ = open_audio_device(waveOutGetNumDevs(), [&](unsigned id) {
    return waveOutOpen(&device_, id, &format, reinterpret_cast<DWORD_PTR>(completed_), 0,
                       CALLBACK_EVENT);
  });
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
  if (device_) {
    waveOutReset(device_);
    for (auto &buffer : buffers_)
      waveOutUnprepareHeader(device_, &buffer.header, sizeof(WAVEHDR));
    waveOutClose(device_);
  }
  if (completed_)
    CloseHandle(completed_);
}

void Audio::frequency(unsigned value) {
  resampler_.frequency(value);
}

void Audio::clear() {
  if (device_)
    waveOutReset(device_);
  buffer_ = offset_ = 0;
  resampler_.clear();
}

void Audio::sample(n64::StereoSample value) {
  if (!available())
    return;
  resampler_.write(value, [this](n64::StereoSample sample) {
    if (!available())
      return;
    auto &buffer = buffers_[buffer_];
    while (buffer.header.dwFlags & WHDR_INQUEUE) {
      if (WaitForSingleObject(completed_, 1000) != WAIT_OBJECT_0) {
        error_ = MMSYSERR_ERROR;
        return;
      }
    }
    const auto convert = [this](double value) {
      return muted ? short(0)
                   : static_cast<short>(std::clamp(value, -1.0, 32767.0 / 32768) * 32768);
    };
    buffer.samples[offset_++] = convert(sample.left);
    buffer.samples[offset_++] = convert(sample.right);
    if (offset_ == buffer.samples.size()) {
      error_ = waveOutWrite(device_, &buffer.header, sizeof(WAVEHDR));
      offset_ = 0;
      buffer_ = (buffer_ + 1) % static_cast<unsigned>(buffers_.size());
    }
  });
}

} // namespace cupid::desktop
