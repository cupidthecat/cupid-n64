#include "core/cartridge/rtc/rtc.hpp"
#include <algorithm>

namespace cupid::n64 {

JoybusStatus Rtc::communicate(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
  if (!present_ || input.empty())
    return {};
  if (input[0] == 6 && output.size() >= 3) {
    output[0] = 0;
    output[1] = 0x10;
    output[2] = status_;
    return {true};
  }
  if (input[0] == 7 && input.size() >= 2 && output.size() >= 9) {
    const auto block = input[1] & 3;
    std::copy_n(data_.begin() + block * 8, 8, output.begin());
    output[8] = status_;
    return {true};
  }
  if (input[0] == 8 && input.size() >= 10 && !output.empty()) {
    const auto block = input[1] & 3;
    if (!(write_lock_ & (1 << block))) {
      std::copy_n(input.begin() + 2, 8, data_.begin() + block * 8);
      if (!block) {
        const auto control = (unsigned(data_[0]) << 8) | data_[1];
        write_lock_ = static_cast<std::uint8_t>((control >> 7) & 6);
        run(!(control & 4));
      }
    }
    output[0] = status_;
    return {true};
  }
  return {};
}

} // namespace cupid::n64
