#pragma once

#include <string>
#include <vector>

namespace cupid::desktop {

struct AudioDevice {
  unsigned id;
  std::wstring identity, name;
};

std::vector<AudioDevice> audio_devices();

} // namespace cupid::desktop
