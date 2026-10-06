#pragma once

#include <windows.h>

#include <mmsystem.h>

namespace cupid::desktop {

template <class Open> MMRESULT open_audio_device(unsigned count, Open open) {
  auto result = open(WAVE_MAPPER);
  for (unsigned id = 0; result != MMSYSERR_NOERROR && id < count; ++id)
    result = open(id);
  return result;
}

} // namespace cupid::desktop
