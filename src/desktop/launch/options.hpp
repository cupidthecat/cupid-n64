#pragma once

#include "desktop/launch/files.hpp"

namespace cupid::desktop {

struct Options : LaunchFiles {
  std::filesystem::path capture;
  std::filesystem::path input_replay, field_trace, capture_fields;
  unsigned frames = 0;
  bool test_input = false;
};

} // namespace cupid::desktop
