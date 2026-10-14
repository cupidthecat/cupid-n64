#pragma once

#include <mmdeviceapi.h>
#include <objbase.h>
#include <string>
#include <wrl/client.h>

namespace cupid::desktop {

class AudioApartment {
public:
  AudioApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~AudioApartment() {
    if (SUCCEEDED(result_))
      CoUninitialize();
  }
  bool available() const {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

private:
  HRESULT result_;
};

Microsoft::WRL::ComPtr<IMMDeviceEnumerator> audio_endpoints();
std::wstring endpoint_identity(IMMDevice &device);
std::wstring endpoint_name(IMMDevice &device);
Microsoft::WRL::ComPtr<IMMDevice> select_endpoint(IMMDeviceEnumerator &enumerator,
                                                  const std::wstring &preferred);

} // namespace cupid::desktop
