#pragma once

#include "desktop/controls/input.hpp"
#include <algorithm>
#include <array>

namespace cupid::desktop {

class AbsoluteMouseMotion {
public:
  PortInput input(bool virtual_desktop, std::int32_t x, std::int32_t y, std::int32_t width,
                  std::int32_t height) {
    PortInput input;
    auto &previous = positions_[virtual_desktop ? 1 : 0];
    if (width <= 0 || height <= 0) {
      previous = {};
      return input;
    }
    const auto current_x = scale(x, width), current_y = scale(y, height);
    if (previous.valid && previous.width == width && previous.height == height) {
      input.mouse_x = current_x - previous.x;
      input.mouse_y = current_y - previous.y;
    }
    previous = {true, current_x, current_y, width, height};
    return input;
  }

  void clear() {
    positions_ = {};
  }

private:
  static std::int32_t scale(std::int32_t normalized, std::int32_t extent) {
    return static_cast<std::int32_t>(
        (std::int64_t(std::clamp(normalized, 0, 65535)) * extent + 32767) / 65535);
  }

  struct Position {
    bool valid = false;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
  };
  std::array<Position, 2> positions_{};
};

} // namespace cupid::desktop
