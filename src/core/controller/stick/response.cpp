#include "core/controller/stick/response.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cupid::n64 {

StickPosition stick_response(std::int16_t x, std::int16_t y) {
  constexpr double cardinal = 85, diagonal = 69, deadzone = 7;
  const double half_span = (diagonal + deadzone) / std::numbers::sqrt2;
  // A full circular diagonal reaches 69 after the axial deadzone.
  const double radius =
      half_span + std::sqrt(half_span * half_span - std::numbers::sqrt2 * diagonal * deadzone);
  auto axis = [&](std::int16_t host) {
    const double position = host * radius / 32767;
    const double distance = std::abs(position);
    if (distance <= deadzone)
      return 0.0;
    return std::copysign((distance - deadzone) * radius / (radius - deadzone), position);
  };
  double ax = axis(x), ay = axis(y);
  const double length = std::hypot(ax, ay);
  if (length > radius) {
    const double scale = radius / length;
    ax *= scale;
    ay *= scale;
  }
  const double slope = (cardinal - diagonal) / diagonal;
  const double extent =
      std::max(std::abs(ax) + slope * std::abs(ay), std::abs(ay) + slope * std::abs(ax));
  if (extent > cardinal) {
    const double scale = cardinal / extent;
    ax *= scale;
    ay *= scale;
  }
  auto coordinate = [&](double value) {
    value = std::clamp(value, -cardinal, cardinal);
    return static_cast<std::int8_t>(value + std::copysign(1e-9, value));
  };
  return {coordinate(ax), coordinate(ay)};
}

} // namespace cupid::n64
