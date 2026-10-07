#include "core/controller/mouse/mouse.hpp"
#include <algorithm>
#include <array>

namespace cupid::n64 {

void Mouse::input(bool left, bool right, std::int32_t x, std::int32_t y) {
  left_ = left;
  right_ = right;
  x_ = static_cast<std::int8_t>(std::clamp(x, -128, 127));
  y_ =
      static_cast<std::int8_t>(std::clamp(-std::int64_t(y), std::int64_t(-128), std::int64_t(127)));
}

std::uint32_t Mouse::state() const {
  return (std::uint32_t(left_) << 31) | (std::uint32_t(right_) << 30) |
         (std::uint32_t(static_cast<std::uint8_t>(x_)) << 8) | static_cast<std::uint8_t>(y_);
}

JoybusStatus Mouse::communicate(std::span<const std::uint8_t> input,
                                std::span<std::uint8_t> output) {
  if (input.empty())
    return {};
  if (input[0] == 0 || input[0] == 255) {
    constexpr std::array<std::uint8_t, 3> identity{2, 0, 2};
    std::copy_n(identity.begin(), std::min(output.size(), identity.size()), output.begin());
    return {true, false};
  }
  if (input[0] == 1) {
    const auto value = state();
    for (unsigned byte = 0; byte < std::min(output.size(), std::size_t(4)); ++byte)
      output[byte] = static_cast<std::uint8_t>(value >> ((3 - byte) * 8));
    return {true, output.size() > 4};
  }
  return {};
}

} // namespace cupid::n64
