#pragma once

#include "core/devices/joybus/device.hpp"
#include <array>

namespace cupid::n64 {

struct GameCubeInput {
  std::uint16_t buttons = 0;
  std::int16_t x = 0, y = 0, cx = 0, cy = 0, l = 0, r = 0;
};

class GameCubePad : public JoybusDevice {
public:
  enum Button : std::uint16_t {
    A = 1,
    B = 2,
    X = 4,
    Y = 8,
    Start = 16,
    Left = 0x100,
    Right = 0x200,
    Down = 0x400,
    Up = 0x800,
    Z = 0x1000,
    R = 0x2000,
    L = 0x4000
  };

  void input_host(GameCubeInput input);
  void reset() override;
  bool rumbling() const {
    return rumbling_;
  }
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  friend class CoreState;
  void sample();
  std::array<std::uint8_t, 8> report(unsigned mode) const;
  GameCubeInput input_;
  std::array<std::uint8_t, 6> analog_{127, 127, 127, 127, 0, 0};
  std::uint16_t buttons_ = 0;
  bool origin_pending_ = true;
  bool rumbling_ = false;
};

} // namespace cupid::n64
