#pragma once

#include <array>
#include <cstdint>

namespace cupid::desktop {

struct PortInput {
  std::uint16_t buttons = 0;
  std::int16_t x = 0, y = 0;
  bool calibrated_stick = false;
  std::uint16_t gamecube_buttons = 0;
  std::int16_t cx = 0, cy = 0, l = 0, r = 0;
  bool mouse_left = false, mouse_right = false;
  std::int32_t mouse_x = 0, mouse_y = 0;
};

using PlayerInputs = std::array<PortInput, 4>;

} // namespace cupid::desktop
