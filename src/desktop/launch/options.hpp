#pragma once

#include "desktop/launch/files.hpp"

namespace cupid::desktop {

struct Options : LaunchFiles {
  std::filesystem::path capture;
  unsigned frames = 0;
  bool test_input = false;
};

} // namespace cupid::desktop
