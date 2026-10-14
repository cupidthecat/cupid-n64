#include "desktop/audio/frame_queue.hpp"
#include "desktop/audio/output.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio/endpoints.hpp"
#include "desktop/windows/audio/identity.hpp"
#include <atomic>
#include <audioclient.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace cupid::desktop {
namespace {
using Microsoft::WRL::ComPtr;

class PlaybackNotifications final : public IMMNotificationClient {
public:
  explicit PlaybackNotifications(std::shared_ptr<std::atomic<bool>> changed)
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

class WasapiAudioOutput final : public AudioOutput {
public:
  WasapiAudioOutput() {
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (apartment_.available() && event_) {
      enumerator_ = audio_endpoints();
      notifications_.Attach(new PlaybackNotifications(changed_));
      if (enumerator_)
        enumerator_->RegisterEndpointNotificationCallback(notifications_.Get());
      open();
    } else
      fail(E_FAIL);
  }
  ~WasapiAudioOutput() override {
    close();
    if (enumerator_ && notifications_)
      enumerator_->UnregisterEndpointNotificationCallback(notifications_.Get());
    notifications_.Reset();
    if (event_)
      CloseHandle(event_);
  }
  void configure(const Audio::Settings &settings) override {
    const bool reopen =
        settings.latency != settings_.latency || settings.device != settings_.device;
    settings_ = settings;
    if (reopen || !available())
      open();
  }
  bool recover() override {
    const bool changed = changed_->exchange(false);
    if (!changed && available())
      return false;
    const auto selected = endpoint();
    if (available() && selected && endpoint_identity(*selected.Get()) == identity_) {
      name_ = endpoint_name(*selected.Get());
      return false;
    }
    open();
    return true;
  }
  void probe() override {
    const auto selected = endpoint();
    if (!available() || !selected || endpoint_identity(*selected.Get()) != identity_)
      changed_->store(true);
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
    queue_.clear();
    if (!available())
      return;
    if (!check(client_->Stop()) || !check(client_->Reset()))
      return;
    ResetEvent(event_);
    check(client_->Start());
  }
  bool wait(unsigned) override {
    static thread_local AudioApartment producer_apartment;
    if (!producer_apartment.available() || !available() || interrupted())
      return false;
    auto progress = std::chrono::steady_clock::now();
    while (queue_.size() >= frames_) {
      const auto result = WaitForSingleObject(event_, settings_.blocking ? 10 : 0);
      if (interrupted())
        return false;
      if (result == WAIT_OBJECT_0) {
        const auto before = queue_.size();
        if (!write())
          return false;
        if (queue_.size() < before)
          progress = std::chrono::steady_clock::now();
      } else if (result != WAIT_TIMEOUT) {
        fail(HRESULT_FROM_WIN32(GetLastError()));
        return false;
      }
      if (!settings_.blocking) {
        // Retain the newest device buffer when output cannot accept another frame.
        if (queue_.size() >= frames_)
          queue_.discard(queue_.size() - frames_ + 1);
        return true;
      }
      if (std::chrono::steady_clock::now() - progress >= std::chrono::seconds(1)) {
        fail(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        return false;
      }
    }
    return true;
  }
  bool floating_samples() const override {
    return true;
  }
  bool wait_after_submit() const override {
    return true;
  }
  void submit_float(std::span<const float> samples) override {
    if (available())
      queue_.append(samples);
  }
  void submit(unsigned, std::span<const short> samples) override {
    std::vector<float> converted;
    converted.reserve(samples.size());
    for (const auto sample : samples)
      converted.push_back(sample / 32768.0f);
    submit_float(converted);
  }
  void wait_interrupt(std::function<bool()> predicate) override {
    interrupt_ = std::move(predicate);
  }
  bool available() const override {
    return opened_.load() && SUCCEEDED(error_.load());
  }
  const std::wstring &device_name() const override {
    return name_;
  }
  bool using_fallback() const override {
    return available() && fallback_;
  }
  std::wstring error() const override {
    std::wostringstream text;
    text << L"Windows audio output failed (0x" << std::hex << static_cast<unsigned>(error_.load())
         << L").";
    return text.str();
  }

private:
  bool interrupted() const {
    return interrupt_ && interrupt_();
  }
  ComPtr<IMMDevice> endpoint() const {
    return enumerator_ ? select_endpoint(*enumerator_.Get(), settings_.device)
                       : ComPtr<IMMDevice>{};
  }
  bool check(HRESULT result) {
    if (SUCCEEDED(result))
      return true;
    fail(result);
    return false;
  }
  void fail(HRESULT result) {
    error_ = result;
    opened_ = false;
    changed_->store(true);
  }
  void close() {
    opened_ = false;
    if (client_)
      client_->Stop();
    render_.Reset();
    client_.Reset();
    queue_.clear();
    frames_ = 0;
  }
  void open() {
    close();
    error_ = S_OK;
    changed_->store(false);
    const auto selected = endpoint();
    if (!selected || !event_) {
      fail(AUDCLNT_E_DEVICE_INVALIDATED);
      return;
    }
    if (!check(selected->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(client_.GetAddressOf()))))
      return;
    REFERENCE_TIME period = 0;
    if (!check(client_->GetDevicePeriod(&period, nullptr)))
      return;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = AudioResampler::output_frequency;
    format.wBitsPerSample = 32;
    format.nBlockAlign = 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    const auto latency = std::max<REFERENCE_TIME>(period * 2, settings_.latency * 10000ll);
    if (!check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                       AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                       AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                   latency, 0, &format, nullptr)) ||
        !check(client_->SetEventHandle(event_)) || !check(client_->GetBufferSize(&frames_)) ||
        !check(client_->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void **>(render_.GetAddressOf()))))
      return;
    if (!frames_ || frames_ > 65536) {
      fail(E_INVALIDARG);
      return;
    }
    identity_ = endpoint_identity(*selected.Get());
    name_ = endpoint_name(*selected.Get());
    fallback_ = !settings_.device.empty() && !same_audio_endpoint(identity_, settings_.device);
    ResetEvent(event_);
    if (check(client_->Start()))
      opened_ = true;
  }
  bool write() {
    UINT32 padding = 0;
    if (!check(client_->GetCurrentPadding(&padding)))
      return false;
    if (padding > frames_) {
      fail(E_UNEXPECTED);
      return false;
    }
    const auto count = static_cast<UINT32>(std::min<std::size_t>(frames_ - padding, queue_.size()));
    if (!count)
      return true;
    BYTE *data = nullptr;
    if (!check(render_->GetBuffer(count, &data)))
      return false;
    queue_.read({reinterpret_cast<float *>(data), std::size_t(count) * 2});
    return check(render_->ReleaseBuffer(count, 0));
  }
  AudioApartment apartment_;
  ComPtr<IMMDeviceEnumerator> enumerator_;
  ComPtr<PlaybackNotifications> notifications_;
  ComPtr<IAudioClient> client_;
  ComPtr<IAudioRenderClient> render_;
  HANDLE event_ = nullptr;
  AudioFrameQueue queue_;
  Audio::Settings settings_;
  std::function<bool()> interrupt_;
  std::shared_ptr<std::atomic<bool>> changed_ = std::make_shared<std::atomic<bool>>(false);
  std::atomic<bool> opened_{false};
  std::atomic<HRESULT> error_{S_OK};
  UINT32 frames_ = 0;
  std::wstring identity_, name_;
  bool fallback_ = false;
};
} // namespace

std::unique_ptr<AudioOutput> make_wasapi_audio_output() {
  return std::make_unique<WasapiAudioOutput>();
}
} // namespace cupid::desktop
