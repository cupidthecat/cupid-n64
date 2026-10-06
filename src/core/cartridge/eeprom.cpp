#include "core/cartridge/eeprom.hpp"

namespace cupid::n64 {

Eeprom::Eeprom(EventQueue &events, unsigned size)
    : events_(events), data_(size == 512 || size == 2048 ? size : 0, 255) {}

JoybusStatus Eeprom::communicate(std::span<const std::uint8_t> input,
                                 std::span<std::uint8_t> output) {
  if (data_.empty() || input.empty())
    return {};
  if (input[0] == 0 || input[0] == 255) {
    if (!output.empty())
      output[0] = 0;
    if (output.size() > 1)
      output[1] = data_.size() == 512 ? 0x80 : 0xc0;
    if (output.size() > 2)
      output[2] = busy_ ? 0x80 : 0;
    return {true};
  }
  if (input[0] == 4 && input.size() >= 2) {
    unsigned address = input[1] * 8;
    for (auto &value : output)
      value = busy_ ? 255 : data_[(address++) & (data_.size() - 1)];
    return {true};
  }
  if (input[0] == 5 && input.size() >= 2 && !output.empty()) {
    output[0] = busy_ ? 0x80 : 0;
    if (!busy_) {
      unsigned address = input[1] * 8;
      for (unsigned n = 2; n < input.size(); ++n)
        data_[(address++) & (data_.size() - 1)] = input[n];
      busy_ = true;
      events_.insert(Event::EepromWrite, 187500 * 6);
    }
    return {true};
  }
  return {};
}

} // namespace cupid::n64
