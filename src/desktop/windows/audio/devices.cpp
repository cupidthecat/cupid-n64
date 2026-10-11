#include "desktop/windows/audio/devices.hpp"
#include <windows.h>

#include <mmsystem.h>

#include <mmddk.h>

namespace cupid::desktop {

std::vector<AudioDevice> audio_devices() {
  std::vector<AudioDevice> result;
  const auto count = waveOutGetNumDevs();
  for (unsigned id = 0; id < count && result.size() < 256; ++id) {
    WAVEOUTCAPSW caps{};
    if (waveOutGetDevCapsW(id, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
      continue;
    const auto handle = reinterpret_cast<HWAVEOUT>(UINT_PTR(id));
    ULONG bytes = 0;
    std::wstring identity;
    if (waveOutMessage(handle, DRV_QUERYDEVICEINTERFACESIZE, reinterpret_cast<DWORD_PTR>(&bytes),
                       0) == MMSYSERR_NOERROR &&
        bytes >= sizeof(wchar_t) && bytes <= 65536 && bytes % sizeof(wchar_t) == 0) {
      identity.resize(bytes / sizeof(wchar_t), L'\0');
      if (waveOutMessage(handle, DRV_QUERYDEVICEINTERFACE,
                         reinterpret_cast<DWORD_PTR>(identity.data()), bytes) == MMSYSERR_NOERROR) {
        const auto end = identity.find(L'\0');
        if (end != std::wstring::npos)
          identity.resize(end);
        else
          identity.clear();
      } else
        identity.clear();
    }
    if (identity.empty())
      identity = L"Name:" + std::wstring(caps.szPname) + L":" + std::to_wstring(caps.wMid) + L":" +
                 std::to_wstring(caps.wPid);
    result.push_back({id, std::move(identity), caps.szPname});
  }
  return result;
}

} // namespace cupid::desktop
