#pragma once

#include <cstdint>

namespace cupid::n64 {

struct StickPosition {
  std::int8_t x, y;
};

// Applies the N64 deadzone and gate while keeping the host axis orientation.
StickPosition stick_response(std::int16_t x, std::int16_t y);

} // namespace cupid::n64
