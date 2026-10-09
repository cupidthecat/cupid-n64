#pragma once

#include <cstdint>

namespace cupid::n64 {

struct StickPosition {
  std::int8_t x, y;
};

enum class StickGate { Nintendo64, GameCubeMain, GameCubeC };

// Applies the selected deadzone and gate while keeping the host axis orientation.
StickPosition stick_response(std::int16_t x, std::int16_t y,
                             StickGate gate = StickGate::Nintendo64);

} // namespace cupid::n64
