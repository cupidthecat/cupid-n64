#pragma once

#include "core/devices/joybus/device.hpp"
#include <functional>
#include <utility>

namespace cupid::n64 {

class Mouse : public JoybusDevice {
public:
  struct Input {
    bool left = false, right = false;
    std::int32_t x = 0, y = 0;
  };
  void input_reader(std::function<Input()> reader) {
    input_reader_ = std::move(reader);
  }
  void input(bool left, bool right, std::int32_t x, std::int32_t y);
  std::uint32_t state() const;
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  friend class CoreState;
  std::function<Input()> input_reader_;
  std::int8_t x_ = 0;
  std::int8_t y_ = 0;
  bool left_ = false;
  bool right_ = false;
};

} // namespace cupid::n64
