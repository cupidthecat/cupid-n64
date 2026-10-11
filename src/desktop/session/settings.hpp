#pragma once

#include "desktop/storage/paths.hpp"
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
};

} // namespace cupid::desktop
