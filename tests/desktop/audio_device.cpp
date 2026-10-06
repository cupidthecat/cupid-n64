#include "desktop/windows/audio_device.hpp"
#include <iostream>
#include <vector>

int main() {
  unsigned failures = 0;
  const auto check = [&](bool condition, const char *name) {
    if (!condition) {
      std::cerr << name << '\n';
      ++failures;
    }
  };
  std::vector<unsigned> attempts;
  auto result = cupid::desktop::open_audio_device(3, [&](unsigned id) -> MMRESULT {
    attempts.push_back(id);
    return id == WAVE_MAPPER ? MMSYSERR_BADDEVICEID : MMSYSERR_NOERROR;
  });
  check(result == MMSYSERR_NOERROR, "Missing default device must fall back to a working output");
  check(attempts == std::vector<unsigned>{WAVE_MAPPER, 0}, "First working output is selected");
  attempts.clear();
  result = cupid::desktop::open_audio_device(3, [&](unsigned id) -> MMRESULT {
    attempts.push_back(id);
    return MMSYSERR_NOERROR;
  });
  check(result == MMSYSERR_NOERROR && attempts == std::vector<unsigned>{WAVE_MAPPER},
        "A working default device remains selected");
  attempts.clear();
  result = cupid::desktop::open_audio_device(3, [&](unsigned id) -> MMRESULT {
    attempts.push_back(id);
    return id == 1 ? MMSYSERR_NOERROR : MMSYSERR_ALLOCATED;
  });
  check(result == MMSYSERR_NOERROR && attempts == std::vector<unsigned>{WAVE_MAPPER, 0, 1},
        "Unavailable outputs are skipped");
  attempts.clear();
  result = cupid::desktop::open_audio_device(2, [&](unsigned id) -> MMRESULT {
    attempts.push_back(id);
    return MMSYSERR_NODRIVER;
  });
  check(result == MMSYSERR_NODRIVER && attempts == std::vector<unsigned>{WAVE_MAPPER, 0, 1},
        "All unavailable outputs preserve the error without retrying forever");
  attempts.clear();
  result = cupid::desktop::open_audio_device(0, [&](unsigned id) -> MMRESULT {
    attempts.push_back(id);
    return MMSYSERR_BADDEVICEID;
  });
  check(result == MMSYSERR_BADDEVICEID && attempts == std::vector<unsigned>{WAVE_MAPPER},
        "A system without outputs reports the default device error");
  return failures ? 1 : 0;
}
