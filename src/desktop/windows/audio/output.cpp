#include "desktop/audio/output.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio_device.hpp"
#include <algorithm>

namespace cupid::desktop {
namespace {

class WindowsAudioOutput final : public AudioOutput {
public:
  explicit WindowsAudioOutput(std::span<short *const> buffers) {
    std::copy(buffers.begin(), buffers.end(), rendered_.begin());
    completed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completed_) {
      error_ = MMSYSERR_NOMEM;
      return;
    }
    open();
  }
  ~WindowsAudioOutput() override {
    close();
    if (completed_)
      CloseHandle(completed_);
  }
  void configure(const Audio::Settings &settings) override {
    const bool reopen = settings.device != settings_.device;
    settings_ = settings;
    if (reopen || !available()) {
      close();
      open();
    }
  }
  bool recover() override {
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
  void clear() override {
    if (device_)
      waveOutReset(device_);
  }
  bool wait(unsigned buffer) override {
    while (headers_[buffer].dwFlags & WHDR_INQUEUE) {
      if (WaitForSingleObject(completed_, 1000) != WAIT_OBJECT_0) {
        error_ = MMSYSERR_ERROR;
        return false;
      }
    }
    return true;
  }
  void submit(unsigned buffer, std::span<const short> samples) override {
    auto &header = headers_[buffer];
    header.dwBufferLength = static_cast<DWORD>(samples.size_bytes());
    error_ = waveOutWrite(device_, &header, sizeof(WAVEHDR));
  }
  bool available() const override {
    return device_ != nullptr && error_ == MMSYSERR_NOERROR;
  }
  const std::wstring &device_name() const override {
    return device_name_;
  }
  bool using_fallback() const override {
    return available() && !settings_.device.empty() && settings_.device != device_identity_;
  }
  std::wstring error() const override {
    std::array<wchar_t, MAXERRORLENGTH> text{};
    waveOutGetErrorTextW(error_, text.data(), static_cast<UINT>(text.size()));
    return text.data();
  }

private:
  void open() {
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
    for (unsigned n = 0; n < headers_.size(); ++n) {
      auto &header = headers_[n];
      header = {};
      header.lpData = reinterpret_cast<char *>(rendered_[n]);
      header.dwBufferLength = 960 * sizeof(short);
      error_ = waveOutPrepareHeader(device_, &header, sizeof(WAVEHDR));
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
  void close() {
    if (device_) {
      waveOutReset(device_);
      for (auto &header : headers_)
        if (header.dwFlags & WHDR_PREPARED)
          waveOutUnprepareHeader(device_, &header, sizeof(WAVEHDR));
      waveOutClose(device_);
      device_ = nullptr;
    }
    device_name_.clear();
    device_identity_.clear();
  }
  std::array<short *, 32> rendered_{};
  std::array<WAVEHDR, 32> headers_{};
  HWAVEOUT device_ = nullptr;
  HANDLE completed_ = nullptr;
  MMRESULT error_ = MMSYSERR_NOERROR;
  Audio::Settings settings_;
  std::wstring device_name_, device_identity_;
  bool preferred_present_ = false;
};

} // namespace

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const> buffers) {
  return std::make_unique<WindowsAudioOutput>(buffers);
}

} // namespace cupid::desktop
