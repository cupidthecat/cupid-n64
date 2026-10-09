#pragma once

#include <cstdint>
#include <vector>

namespace cupid::n64 {

struct VideoFrame {
  unsigned width = 0, height = 0;
  std::vector<std::uint8_t> rgba;
};

} // namespace cupid::n64
