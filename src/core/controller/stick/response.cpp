#include "core/controller/stick/response.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cupid::n64 {
namespace {

StickPosition arcade_response(std::int16_t x, std::int16_t y) {
  constexpr double cardinal = 85, diagonal = 69, deadzone = 7;
  const double radius =
      2.0 / std::numbers::sqrt2 * (diagonal / cardinal * (cardinal - deadzone) + deadzone);
  double ax = x * radius / 32767, ay = y * radius / 32767;
  double length = std::sqrt(ax * ax + ay * ay);
  if (length <= radius) {
    const auto axis = [&](double value) {
      const double distance = std::abs(value);
      return distance <= deadzone
                 ? 0.0
                 : value * ((distance - deadzone) * cardinal / (cardinal - deadzone) / distance);
    };
    ax = axis(ax);
    ay = axis(ay);
  } else {
    const double scale = radius / length;
    ax *= scale;
    ay *= scale;
  }
  if (ax != 0 && ay != 0) {
    const double slope = ay / ax;
    double edge_x =
        std::copysign(cardinal / (std::abs(slope) + (cardinal - diagonal) / diagonal), ax);
    const double edge_y = std::copysign(
        std::min(std::abs(edge_x * slope),
                 cardinal / (1.0 / std::abs(slope) + (cardinal - diagonal) / diagonal)),
        ay);
    edge_x = edge_y / slope;
    length = std::sqrt(ax * ax + ay * ay);
    if (length > std::sqrt(edge_x * edge_x + edge_y * edge_y)) {
      ax = edge_x;
      ay = edge_y;
    }
  }
  const auto coordinate = [&](double value) {
    value = std::clamp(value, -cardinal, cardinal);
    return static_cast<std::int8_t>(std::copysign(std::abs(value) + 1e-9, value));
  };
  return {coordinate(ax), coordinate(ay)};
}

} // namespace

StickPosition stick_response(std::int16_t x, std::int16_t y, StickGate gate) {
  if (gate == StickGate::Aleck64)
    return arcade_response(x, y);
  const bool n64 = gate == StickGate::Nintendo64;
  const double cardinal = n64 ? 85 : gate == StickGate::GameCubeMain ? 100 : 76;
  const double diagonal = n64 ? 69 : cardinal * 0.75;
  const double deadzone = n64 ? 7 : cardinal * 0.08;
  const double half_span = (diagonal + deadzone) / std::numbers::sqrt2;
  // A full circular diagonal reaches the gate corner after the axial deadzone.
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
