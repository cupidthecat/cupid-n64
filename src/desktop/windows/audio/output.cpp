#include "desktop/audio/sdl/output.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio/endpoints.hpp"
#include "desktop/windows/audio/identity.hpp"
#include <atomic>
#include <mmsystem.h>
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

class WindowsAudioOutput final : public SdlAudioOutput {
public:
  WindowsAudioOutput() : SdlAudioOutput(wide) {
    timeBeginPeriod(1);
    if (apartment_.available()) {
      enumerator_ = audio_endpoints();
      notifications_.Attach(new EndpointNotifications(changed_));
      if (enumerator_)
        enumerator_->RegisterEndpointNotificationCallback(notifications_.Get());
    }
    open();
  }
  ~WindowsAudioOutput() override {
    close();
    if (enumerator_ && notifications_)
      enumerator_->UnregisterEndpointNotificationCallback(notifications_.Get());
    notifications_.Reset();
    enumerator_.Reset();
    timeEndPeriod(1);
  }
  std::vector<Audio::Device> devices() const override {
    std::vector<Audio::Device> result;
    for (auto &device : audio_devices())
      result.push_back({std::move(device.identity), std::move(device.name)});
    return result;
  }

private:
  Endpoint endpoint() const override {
    auto preferred = settings().device;
    for (const auto &device : audio_devices())
      if (same_audio_endpoint(preferred, device.identity)) {
        preferred = device.name;
        break;
      }
    return named_endpoint(preferred);
  }
  AudioApartment apartment_;
  ComPtr<IMMDeviceEnumerator> enumerator_;
  ComPtr<EndpointNotifications> notifications_;
};
} // namespace

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const>) {
  return std::make_unique<WindowsAudioOutput>();
}
} // namespace cupid::desktop
