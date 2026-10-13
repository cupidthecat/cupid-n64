#include "desktop/audio/output.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio/endpoints.hpp"
#include "desktop/windows/audio/identity.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <mmsystem.h>
#include <mutex>
#include <utility>
#include <windows.h>

namespace cupid::desktop {
namespace {
using Microsoft::WRL::ComPtr;

std::wstring wide(const char *text) {
  if (!text)
    return {};
  const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
  if (!count)
    return {};
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), count);
  result.pop_back();
  return result;
}

class EndpointNotifications final : public IMMNotificationClient {
public:
  explicit EndpointNotifications(std::shared_ptr<std::atomic<bool>> changed)
      : changed_(std::move(changed)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **result) override {
    if (!result)
      return E_POINTER;
    *result = nullptr;
    if (id != __uuidof(IUnknown) && id != __uuidof(IMMNotificationClient))
      return E_NOINTERFACE;
    *result = static_cast<IMMNotificationClient *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return ++references_;
  }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining = --references_;
    if (!remaining)
      delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
    return changed();
  }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
    return changed();
  }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
    return changed();
  }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
    return changed();
  }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
    return flow == eRender && role == eMultimedia ? changed() : S_OK;
  }

private:
  HRESULT changed() {
    changed_->store(true);
    return S_OK;
  }
  std::atomic<ULONG> references_{1};
  std::shared_ptr<std::atomic<bool>> changed_;
};

class WindowsAudioOutput final : public AudioOutput {
public:
  WindowsAudioOutput() {
    timeBeginPeriod(1);
    if (apartment_.available()) {
      enumerator_ = audio_endpoints();
      notifications_.Attach(new EndpointNotifications(changed_));
      if (enumerator_)
        enumerator_->RegisterEndpointNotificationCallback(notifications_.Get());
    }
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    initialized_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (initialized_)
      open();
    else
      fail();
  }
  ~WindowsAudioOutput() override {
    close();
    if (enumerator_ && notifications_)
      enumerator_->UnregisterEndpointNotificationCallback(notifications_.Get());
    notifications_.Reset();
    enumerator_.Reset();
    if (initialized_)
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
    timeEndPeriod(1);
  }
  void configure(const Audio::Settings &settings) override {
    const bool changed =
        settings.device != settings_.device || settings.latency != settings_.latency;
    settings_ = settings;
    if (changed || !available())
      open();
  }
  bool recover() override {
    if (!changed_->exchange(false) && available())
      return false;
    const auto desired = endpoint();
    if (available() && desired == selected_)
      return false;
    open();
    return true;
  }
  bool recovery_pending() const override {
    return changed_->load() || !available();
  }
  std::vector<Audio::Device> devices() const override {
    std::vector<Audio::Device> result;
    for (auto &device : audio_devices())
      result.push_back({std::move(device.identity), std::move(device.name)});
    return result;
  }
  void clear() override {
    if (stream_ && !SDL_ClearAudioStream(stream_))
      fail();
  }
  bool wait(unsigned) override {
    if (!available())
      return false;
    const auto limit = static_cast<int>((AudioResampler::output_frequency / 100) * sizeof(float) *
                                        2 * (settings_.latency / 10));
    for (;;) {
      if (interrupt_ && interrupt_())
        return false;
      const auto queued = SDL_GetAudioStreamQueued(stream_);
      if (queued < 0) {
        fail();
        return false;
      }
      if (queued <= limit)
        return true;
      const auto frames = static_cast<std::uint64_t>(queued - limit) / (sizeof(float) * 2);
      const auto delay =
          std::max<std::uint64_t>(1000, frames * 1000000000 / AudioResampler::output_frequency);
      SDL_DelayNS(std::min<std::uint64_t>(delay, 1000000));
      if (!SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_))) {
        fail();
        return false;
      }
    }
  }
  void wait_interrupt(std::function<bool()> predicate) override {
    interrupt_ = std::move(predicate);
  }
  void submit(unsigned, std::span<const short> samples) override {
    std::vector<float> converted;
    converted.reserve(samples.size());
    for (const auto sample : samples)
      converted.push_back(sample / 32768.0f);
    submit_float(converted);
  }
  bool floating_samples() const override {
    return true;
  }
  void submit_float(std::span<const float> samples) override {
    if (!available() || samples.empty())
      return;
    if (samples.size() % 2 ||
        samples.size_bytes() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw std::invalid_argument("Invalid stereo audio buffer size.");
    if (!SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size_bytes())))
      fail();
  }
  bool available() const override {
    return opened_.load();
  }
  const std::wstring &device_name() const override {
    return name_;
  }
  bool using_fallback() const override {
    return available() && fallback_;
  }
  std::wstring error() const override {
    std::lock_guard lock(error_mutex_);
    return error_;
  }

private:
  SDL_AudioDeviceID endpoint() {
    fallback_ = !settings_.device.empty();
    if (settings_.device.empty())
      return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    auto preferred = settings_.device;
    for (const auto &device : audio_devices())
      if (same_audio_endpoint(preferred, device.identity)) {
        preferred = device.name;
        break;
      }
    int count = 0;
    auto devices = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID selected = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    for (int n = 0; n < count; ++n)
      if (wide(SDL_GetAudioDeviceName(devices[n])) == preferred) {
        selected = devices[n];
        fallback_ = false;
        break;
      }
    SDL_free(devices);
    return selected;
  }
  void fail() {
    opened_ = false;
    changed_->store(true);
    std::lock_guard lock(error_mutex_);
    error_ = wide(SDL_GetError());
    if (error_.empty())
      error_ = L"The audio device stopped accepting samples.";
  }
  void open() {
    close();
    if (!initialized_) {
      initialized_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
      if (!initialized_) {
        fail();
        return;
      }
    }
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
    SDL_AudioSpec format{SDL_AUDIO_F32, 2, AudioResampler::output_frequency};
    selected_ = endpoint();
    stream_ = SDL_OpenAudioDeviceStream(selected_, &format, nullptr, nullptr);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
      fail();
      return;
    }
    name_ = wide(SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_)));
    {
      std::lock_guard lock(error_mutex_);
      error_.clear();
    }
    changed_->store(false);
    opened_ = true;
  }
  void close() {
    opened_ = false;
    if (stream_)
      SDL_DestroyAudioStream(std::exchange(stream_, nullptr));
  }
  AudioApartment apartment_;
  ComPtr<IMMDeviceEnumerator> enumerator_;
  ComPtr<EndpointNotifications> notifications_;
  std::shared_ptr<std::atomic<bool>> changed_ = std::make_shared<std::atomic<bool>>(false);
  SDL_AudioStream *stream_ = nullptr;
  SDL_AudioDeviceID selected_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
  Audio::Settings settings_;
  std::function<bool()> interrupt_;
  std::atomic<bool> opened_{false};
  mutable std::mutex error_mutex_;
  std::wstring name_, error_;
  bool initialized_ = false, fallback_ = false;
};
} // namespace

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const>) {
  return std::make_unique<WindowsAudioOutput>();
}
} // namespace cupid::desktop
