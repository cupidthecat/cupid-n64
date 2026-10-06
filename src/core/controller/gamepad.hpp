#pragma once

#include "core/devices/joybus/device.hpp"
#include "core/timing/random.hpp"
#include <vector>

namespace cupid::n64 {

class Gamepad : public JoybusDevice {
public:
  explicit Gamepad(RandomGenerator &random);
  void input(std::uint16_t buttons, std::int8_t x, std::int8_t y);
  void memory_pak(unsigned banks = 1);
  void rumble_pak();
  void disconnect_pak();
  std::uint32_t state() const;
  bool rumbling() const {
    return motor_;
  }
  std::span<std::uint8_t> pak_data() {
    return ram_;
  }
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  enum class Pak { None, Memory, Rumble };
  void format();
  RandomGenerator &random_;
  std::vector<std::uint8_t> ram_;
  Pak pak_ = Pak::None;
  unsigned bank_ = 0;
  std::uint16_t buttons_ = 0;
  std::int8_t x_ = 0;
  std::int8_t y_ = 0;
  bool detect_ = false;
  bool motor_ = false;
};

} // namespace cupid::n64
