#include "core/controller/gamecube/gamecube.hpp"
#include "core/controller/stick/response.hpp"
#include <algorithm>

namespace cupid::n64 {

void GameCubePad::input_host(GameCubeInput input) {
  input_ = input;
}

void GameCubePad::reset() {
  analog_ = {127, 127, 127, 127, 0, 0};
  buttons_ = 0;
  origin_pending_ = true;
  rumbling_ = false;
}

void GameCubePad::sample() {
  const auto main = stick_response(input_.x, input_.y, StickGate::GameCubeMain);
  const auto camera = stick_response(input_.cx, input_.cy, StickGate::GameCubeC);
  auto trigger = [](std::int16_t value) {
    return static_cast<std::uint8_t>(std::clamp(double(value) / 32767 * 200, 0.0, 200.0));
  };
  analog_ = {static_cast<std::uint8_t>(127 + main.x),
             static_cast<std::uint8_t>(127 - main.y),
             static_cast<std::uint8_t>(127 + camera.x),
             static_cast<std::uint8_t>(127 - camera.y),
             trigger(input_.l),
             trigger(input_.r)};
  buttons_ = input_.buttons & 0x7f1f;
  for (const auto pair : {Up | Down, Left | Right})
    if ((buttons_ & pair) == pair)
      buttons_ &= ~pair;
  if (analog_[4] >= 180)
    buttons_ |= L;
  if (analog_[5] >= 180)
    buttons_ |= R;
  if (buttons_ & L)
    analog_[4] = 200;
  if (buttons_ & R)
    analog_[5] = 200;
}

JoybusStatus GameCubePad::communicate(std::span<const std::uint8_t> input,
                                      std::span<std::uint8_t> output) {
  if (input.empty())
    return {};
  std::array<std::uint8_t, 10> response{};
  unsigned length = 0;
  bool overflow = false;
  if (input[0] == 0 || input[0] == 0xff) {
    response[0] = 9;
    response[2] = rumbling_ ? 8 : 0;
    length = 3;
  } else if (input[0] == 0x40 && input.size() >= 3) {
    sample();
    rumbling_ = input[2] & 1;
    const auto value = report(input[1]);
    std::copy(value.begin(), value.end(), response.begin());
    length = 8;
    overflow = output.size() > length;
  } else if (input[0] == 0x41 || input[0] == 0x42) {
    sample();
    response[1] = 0x80;
    std::fill_n(response.begin() + 2, 4, 127);
    origin_pending_ = false;
    length = 10;
    overflow = output.size() > length;
  } else if (input[0] == 0x43) {
    sample();
    const auto value = report(3);
    std::copy(value.begin(), value.end(), response.begin());
    length = 10;
    overflow = output.size() > length;
  } else
    return {};
  std::copy_n(response.begin(), std::min(output.size(), std::size_t(length)), output.begin());
  return {true, overflow};
}

} // namespace cupid::n64
