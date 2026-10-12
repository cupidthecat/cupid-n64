#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/audio/endpoints.hpp"
#include "desktop/windows/audio/identity.hpp"
#include <functiondiscoverykeys_devpkey.h>
#include <windows.h>

namespace cupid::desktop {

Microsoft::WRL::ComPtr<IMMDeviceEnumerator> audio_endpoints() {
  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> result;
  CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&result));
  return result;
}

std::wstring endpoint_identity(IMMDevice &device) {
  LPWSTR text = nullptr;
  if (FAILED(device.GetId(&text)))
    return {};
  const std::wstring result(text);
  CoTaskMemFree(text);
  return result;
}

std::wstring endpoint_name(IMMDevice &device) {
  Microsoft::WRL::ComPtr<IPropertyStore> properties;
  if (FAILED(device.OpenPropertyStore(STGM_READ, &properties)))
    return L"Audio output";
  PROPVARIANT value{};
  const auto result = properties->GetValue(PKEY_Device_FriendlyName, &value);
  const std::wstring name =
      SUCCEEDED(result) && value.vt == VT_LPWSTR ? value.pwszVal : L"Audio output";
  PropVariantClear(&value);
  return name;
}

Microsoft::WRL::ComPtr<IMMDevice> select_endpoint(IMMDeviceEnumerator &enumerator,
                                                  const std::wstring &preferred) {
  Microsoft::WRL::ComPtr<IMMDevice> device;
  if (!preferred.empty()) {
    enumerator.GetDevice(audio_endpoint_key(preferred).c_str(), &device);
    DWORD state = 0;
    if (device && (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE))
      device.Reset();
  }
  if (!device)
    enumerator.GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
  if (!device) {
    Microsoft::WRL::ComPtr<IMMDeviceCollection> active;
    if (SUCCEEDED(enumerator.EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &active))) {
      UINT count = 0;
      active->GetCount(&count);
      for (UINT n = 0; n < count && !device; ++n)
        active->Item(n, &device);
    }
  }
  return device;
}

std::vector<AudioDevice> audio_devices() {
  AudioApartment apartment;
  if (!apartment.available())
    return {};
  const auto enumerator = audio_endpoints();
  Microsoft::WRL::ComPtr<IMMDeviceCollection> devices;
  if (!enumerator || FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
    return {};
  std::vector<AudioDevice> result;
  UINT count = 0;
  devices->GetCount(&count);
  for (unsigned id = 0; id < count && result.size() < 256; ++id) {
    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (FAILED(devices->Item(id, &device)))
      continue;
    result.push_back({id, endpoint_identity(*device.Get()), endpoint_name(*device.Get())});
  }
  return result;
}

} // namespace cupid::desktop
