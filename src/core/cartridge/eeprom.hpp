#pragma once

#include "core/devices/joybus/device.hpp"
#include "core/timing/event_queue.hpp"
#include <vector>

namespace cupid::n64 {

class Eeprom : public JoybusDevice {
public:
  Eeprom(EventQueue &events, unsigned size);
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;
  void complete_write() {
    busy_ = false;
  }
  std::span<std::uint8_t> data() {
    return data_;
  }

private:
  EventQueue &events_;
  std::vector<std::uint8_t> data_;
  bool busy_ = false;
};

} // namespace cupid::n64
