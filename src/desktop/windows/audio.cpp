#include "desktop/windows/audio.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio_device.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

Audio::Audio() {
  completed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!completed_) {
    error_ = MMSYSERR_NOMEM;
    return;
  }
  open();
}

void Audio::open() {
  if (!completed_)
    return;
  const auto devices = audio_devices();
  const auto preferred = std::find_if(devices.begin(), devices.end(), [&](const auto &device) {
    return device.identity == settings_.device;
  });
  preferred_present_ = preferred != devices.end();
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = AudioResampler::output_frequency;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  const auto open_device = [&](unsigned id) {
    return waveOutOpen(&device_, id, &format, reinterpret_cast<DWORD_PTR>(completed_), 0,
                       CALLBACK_EVENT);
  };
  error_ = preferred == devices.end() ? MMSYSERR_BADDEVICEID : open_device(preferred->id);
  if (error_ != MMSYSERR_NOERROR)
    error_ = open_audio_device(waveOutGetNumDevs(), open_device);
  if (error_ != MMSYSERR_NOERROR) {
    device_ = nullptr;
    return;
  }
  for (auto &buffer : buffers_) {
    buffer.header = {};
    buffer.header.lpData = reinterpret_cast<char *>(buffer.rendered.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(sizeof(buffer.samples));
    error_ = waveOutPrepareHeader(device_, &buffer.header, sizeof(WAVEHDR));
    if (error_ != MMSYSERR_NOERROR) {
      close();
      return;
    }
  }
  unsigned id = WAVE_MAPPER;
  waveOutGetID(device_, &id);
  const auto active = std::find_if(devices.begin(), devices.end(),
                                   [&](const auto &device) { return device.id == id; });
  device_name_ = active == devices.end() ? L"System default" : active->name;
  device_identity_ = active == devices.end() ? L"" : active->identity;
}

std::wstring Audio::error() const {
  std::array<wchar_t, MAXERRORLENGTH> text{};
  waveOutGetErrorTextW(error_, text.data(), static_cast<UINT>(text.size()));
  return text.data();
}

Audio::~Audio() {
  close();
  if (completed_)
    CloseHandle(completed_);
}

void Audio::close() {
  if (device_) {
    waveOutReset(device_);
    for (auto &buffer : buffers_)
      if (buffer.header.dwFlags & WHDR_PREPARED)
        waveOutUnprepareHeader(device_, &buffer.header, sizeof(WAVEHDR));
    waveOutClose(device_);
    device_ = nullptr;
  }
  device_name_.clear();
  device_identity_.clear();
}

void Audio::configure(Settings settings) {
  if (settings.levels.volume > 200 || settings.levels.balance < -100 ||
      settings.levels.balance > 100 || settings.latency < 20 || settings.latency > 320 ||
      settings.latency % 10)
    throw std::invalid_argument("Invalid audio volume, balance, or latency.");
  const bool reopen = settings.device != settings_.device;
  const bool reset = reopen || settings.latency != settings_.latency;
  settings_ = std::move(settings);
  if (reset)
    clear();
  if (reopen || !available()) {
    close();
    open();
  }
}

bool Audio::recover() {
  const auto devices = audio_devices();
  const bool present = std::any_of(devices.begin(), devices.end(), [&](const auto &device) {
    return device.identity == device_identity_;
  });
  const bool preferred = std::any_of(devices.begin(), devices.end(), [&](const auto &device) {
    return device.identity == settings_.device;
  });
  const bool returned = preferred && !preferred_present_;
  preferred_present_ = preferred;
  if (available() && present && !(using_fallback() && returned))
    return false;
  clear();
  close();
  open();
  return true;
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
  if (!available() || suspended)
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
      queue(offset_);
    }
  });
}

void Audio::play_saved_output() {
  if (!available() || muted || suspended || !offset_)
    return;
  auto &buffer = buffers_[buffer_];
  while (buffer.header.dwFlags & WHDR_INQUEUE) {
    if (WaitForSingleObject(completed_, 1000) != WAIT_OBJECT_0) {
      error_ = MMSYSERR_ERROR;
      return;
    }
  }
  queue(offset_);
}

void Audio::queue(unsigned samples) {
  auto &buffer = buffers_[buffer_];
  render_audio(std::span(buffer.samples).first(samples), std::span(buffer.rendered).first(samples),
               settings_.levels);
  buffer.header.dwBufferLength = static_cast<DWORD>(samples * sizeof(short));
  error_ = waveOutWrite(device_, &buffer.header, sizeof(WAVEHDR));
  offset_ = 0;
  buffer_ = (buffer_ + 1) % (settings_.latency / 10);
}

} // namespace cupid::desktop
