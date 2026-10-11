#pragma once

#include "desktop/controls/ports.hpp"
#include "desktop/storage/paths.hpp"
#include "renderer/video/settings.hpp"
#include <array>

namespace cupid::desktop {

struct MemorySettings {
  bool expansion = true;
  std::array<unsigned, 4> pak_banks{1, 1, 1, 1};
  static constexpr std::array<unsigned, 4> capacities{1, 4, 16, 62};
};

struct SessionSettings {
  MemorySettings memory;
  StoragePaths storage;
  ControllerPorts ports;
  n64::RenderSettings graphics;
};

} // namespace cupid::desktop
