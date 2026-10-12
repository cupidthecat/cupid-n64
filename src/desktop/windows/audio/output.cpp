#include "desktop/audio/output.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio/endpoints.hpp"
#include "desktop/windows/audio/identity.hpp"
#include <algorithm>
#include <atomic>
#include <audioclient.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

namespace cupid::desktop {
namespace {
using Microsoft::WRL::ComPtr;

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
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    playback_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wake_ || !playback_) {
      error_ = E_OUTOFMEMORY;
      return;
    }
    thread_ = std::thread([this] { run(); });
    request(Operation::Configure, {});
  }
  ~WindowsAudioOutput() override {
    if (thread_.joinable()) {
      request(Operation::Stop, {});
      thread_.join();
    }
    if (wake_)
      CloseHandle(wake_);
    if (playback_)
      CloseHandle(playback_);
  }
  void configure(const Audio::Settings &settings) override {
    if (thread_.joinable())
      request(Operation::Configure, settings);
  }
  bool recover() override {
    return thread_.joinable() && request(Operation::Recover, {});
  }
  bool recovery_pending() const override {
    return changed_->load() || !available();
  }
  std::vector<Audio::Device> devices() const override {
    std::lock_guard lock(mutex_);
    return devices_;
  }
  void clear() override {
    if (thread_.joinable())
      request(Operation::Clear, {});
  }
  bool wait(unsigned) override {
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    if (!space_.wait_until(lock, deadline, [this] {
          return !available() || queued_ + padding_.load() * 2 + 960 <= limit_;
        })) {
      error_ = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
      changed_->store(true);
      return false;
    }
    return available();
  }
  void submit(unsigned, std::span<const short> samples) override {
    if (samples.empty())
      return;
    std::lock_guard lock(mutex_);
    if (!available())
      return;
    if (samples.size() % 2 || samples.size() > queue_.size() - queued_) {
      error_ = E_INVALIDARG;
      return;
    }
    const auto end = (begin_ + queued_) % queue_.size();
    const auto first = std::min(samples.size(), queue_.size() - end);
    std::copy_n(samples.begin(), first, queue_.begin() + end);
    std::copy(samples.begin() + first, samples.end(), queue_.begin());
    queued_ += samples.size();
    SetEvent(wake_);
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
    std::wostringstream result;
    result << L"Windows audio output failed (0x" << std::hex << static_cast<unsigned>(error_.load())
           << L").";
    return result.str();
  }

private:
  enum class Operation { Configure, Clear, Recover, Stop };
  struct Command {
    Operation operation;
    Audio::Settings settings;
  };
  bool request(Operation operation, Audio::Settings settings) {
    std::lock_guard serialized(control_);
    std::unique_lock lock(mutex_);
    command_ = Command{operation, std::move(settings)};
    command_done_ = false;
    SetEvent(wake_);
    command_finished_.wait(lock, [this] { return command_done_; });
    return recovered_;
  }
  void fail(HRESULT result) {
    if (FAILED(result)) {
      error_ = result;
      changed_->store(true);
      space_.notify_all();
    }
  }
  void empty() {
    std::lock_guard lock(mutex_);
    begin_ = queued_ = 0;
    padding_ = 0;
    space_.notify_all();
  }
  void close() {
    opened_ = false;
    if (client_ && started_)
      client_->Stop();
    render_.Reset();
    client_.Reset();
    frames_ = 0;
    started_ = false;
    empty();
  }
  bool open() {
    close();
    error_ = S_OK;
    changed_->store(false);
    refresh_devices();
    auto device =
        enumerator_ ? select_endpoint(*enumerator_.Get(), settings_.device) : ComPtr<IMMDevice>{};
    if (!device) {
      error_ = AUDCLNT_E_DEVICE_INVALIDATED;
      return false;
    }
    const auto identity = endpoint_identity(*device.Get());
    auto result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(client_.GetAddressOf()));
    if (FAILED(result)) {
      fail(result);
      return false;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = AudioResampler::output_frequency;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    result = client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        static_cast<REFERENCE_TIME>(settings_.latency) * 10000, 0, &format, nullptr);
    if (SUCCEEDED(result))
      result = client_->SetEventHandle(playback_);
    if (SUCCEEDED(result))
      result = client_->GetBufferSize(&frames_);
    if (SUCCEEDED(result))
      result = client_->GetService(IID_PPV_ARGS(&render_));
    if (FAILED(result)) {
      fail(result);
      close();
      return false;
    }
    identity_ = identity;
    name_ = endpoint_name(*device.Get());
    fallback_ = !settings_.device.empty() && !same_audio_endpoint(settings_.device, identity);
    {
      std::lock_guard lock(mutex_);
      limit_ = static_cast<std::size_t>(settings_.latency) * 96;
    }
    opened_ = true;
    return true;
  }
  bool recover_endpoint() {
    if (!changed_->exchange(false) && available())
      return false;
    refresh_devices();
    auto device =
        enumerator_ ? select_endpoint(*enumerator_.Get(), settings_.device) : ComPtr<IMMDevice>{};
    if (available() && device && endpoint_identity(*device.Get()) == identity_) {
      name_ = endpoint_name(*device.Get());
      return false;
    }
    open();
    return true;
  }
  void refresh_devices() {
    std::vector<Audio::Device> devices;
    for (auto &device : audio_devices())
      devices.push_back({std::move(device.identity), std::move(device.name)});
    std::lock_guard lock(mutex_);
    devices_ = std::move(devices);
  }
  void pump() {
    if (!available())
      return;
    UINT32 padding = 0;
    auto result = client_->GetCurrentPadding(&padding);
    if (FAILED(result)) {
      fail(result);
      return;
    }
    std::unique_lock lock(mutex_);
    padding_ = padding;
    if (padding > frames_) {
      lock.unlock();
      fail(E_UNEXPECTED);
      return;
    }
    const auto frames = static_cast<UINT32>(std::min<std::size_t>(queued_ / 2, frames_ - padding));
    if (frames) {
      BYTE *destination = nullptr;
      result = render_->GetBuffer(frames, &destination);
      if (FAILED(result)) {
        lock.unlock();
        fail(result);
        return;
      }
      const auto samples = static_cast<std::size_t>(frames) * 2;
      const auto first = std::min(samples, queue_.size() - begin_);
      std::memcpy(destination, queue_.data() + begin_, first * sizeof(short));
      std::memcpy(destination + first * sizeof(short), queue_.data(),
                  (samples - first) * sizeof(short));
      result = render_->ReleaseBuffer(frames, 0);
      if (FAILED(result)) {
        lock.unlock();
        fail(result);
        return;
      }
      begin_ = (begin_ + samples) % queue_.size();
      queued_ -= samples;
      padding_ = padding + frames;
      if (!started_) {
        result = client_->Start();
        if (FAILED(result)) {
          lock.unlock();
          fail(result);
          return;
        }
        started_ = true;
      }
    }
    lock.unlock();
    space_.notify_all();
  }
  void run() {
    AudioApartment apartment;
    if (apartment.available()) {
      enumerator_ = audio_endpoints();
      notifications_.Attach(new EndpointNotifications(changed_));
      if (enumerator_)
        enumerator_->RegisterEndpointNotificationCallback(notifications_.Get());
    }
    for (;;) {
      const HANDLE events[]{wake_, playback_};
      const auto signaled = WaitForMultipleObjects(2, events, FALSE, INFINITE);
      if (signaled == WAIT_FAILED)
        fail(HRESULT_FROM_WIN32(GetLastError()));
      std::optional<Command> command;
      {
        std::lock_guard lock(mutex_);
        command = std::move(command_);
        command_.reset();
      }
      if (command) {
        bool recovered = false;
        switch (command->operation) {
        case Operation::Configure: {
          const bool changed = command->settings.device != settings_.device ||
                               command->settings.latency != settings_.latency;
          settings_ = std::move(command->settings);
          if (changed || !available())
            open();
          break;
        }
        case Operation::Recover:
          recovered = recover_endpoint();
          break;
        case Operation::Clear:
          if (client_) {
            if (started_)
              fail(client_->Stop());
            started_ = false;
            fail(client_->Reset());
          }
          empty();
          break;
        case Operation::Stop:
          close();
          break;
        }
        {
          std::lock_guard lock(mutex_);
          recovered_ = recovered;
          command_done_ = true;
        }
        command_finished_.notify_all();
        if (command->operation == Operation::Stop)
          break;
      }
      pump();
    }
    // COM rendering interfaces are created, used, and released by this thread.
    close();
    if (enumerator_ && notifications_)
      enumerator_->UnregisterEndpointNotificationCallback(notifications_.Get());
    notifications_.Reset();
    enumerator_.Reset();
  }
  HANDLE wake_ = nullptr, playback_ = nullptr;
  mutable std::mutex mutex_;
  std::mutex control_;
  std::vector<Audio::Device> devices_;
  std::condition_variable space_, command_finished_;
  std::optional<Command> command_;
  bool command_done_ = false, recovered_ = false;
  std::atomic<bool> opened_{false};
  std::atomic<HRESULT> error_{S_OK};
  std::atomic<UINT32> padding_{0};
  std::shared_ptr<std::atomic<bool>> changed_ = std::make_shared<std::atomic<bool>>(false);
  std::array<short, 30720> queue_{};
  std::size_t begin_ = 0, queued_ = 0, limit_ = 7680;
  Audio::Settings settings_;
  std::wstring identity_, name_;
  bool fallback_ = false, started_ = false;
  UINT32 frames_ = 0;
  ComPtr<IMMDeviceEnumerator> enumerator_;
  ComPtr<EndpointNotifications> notifications_;
  ComPtr<IAudioClient> client_;
  ComPtr<IAudioRenderClient> render_;
  std::thread thread_;
};
} // namespace
std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const>) {
  return std::make_unique<WindowsAudioOutput>();
}
} // namespace cupid::desktop
