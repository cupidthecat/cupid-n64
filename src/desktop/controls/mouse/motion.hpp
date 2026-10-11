#pragma once

#include "desktop/controls/input.hpp"
#include <algorithm>
#include <limits>

namespace cupid::desktop {

class MouseMotion {
public:
  void input(bool left, bool right, std::int32_t x, std::int32_t y) {
    left_ = left;
    right_ = right;
    x_ = accumulate(x_, x);
    y_ = accumulate(y_, y);
  }

  PortInput take() {
    return consume(
        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
  }

  PortInput read() {
    return consume(-128, 127, -127, 128);
  }

  void clear() {
    left_ = right_ = false;
    x_ = y_ = 0;
  }

private:
  static std::int32_t accumulate(std::int32_t current, std::int32_t delta) {
    return static_cast<std::int32_t>(std::clamp(
        std::int64_t(current) + delta, std::int64_t(std::numeric_limits<std::int32_t>::min()),
        std::int64_t(std::numeric_limits<std::int32_t>::max())));
  }

  PortInput consume(std::int32_t xmin, std::int32_t xmax, std::int32_t ymin, std::int32_t ymax) {
    PortInput input;
    input.mouse_left = left_;
    input.mouse_right = right_;
    input.mouse_x = std::clamp(x_, xmin, xmax);
    input.mouse_y = std::clamp(y_, ymin, ymax);
    x_ -= input.mouse_x;
    y_ -= input.mouse_y;
    return input;
  }

  bool left_ = false, right_ = false;
  std::int32_t x_ = 0, y_ = 0;
};

} // namespace cupid::desktop
