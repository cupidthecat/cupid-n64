#pragma once

#include <array>
#include <cstdint>

namespace cupid::n64 {

struct RspVector {
  std::array<std::uint16_t, 8> lanes{};
  std::uint8_t byte(unsigned index) const {
    return static_cast<std::uint8_t>(lanes[index >> 1] >> ((1 - (index & 1)) * 8));
  }
  void byte(unsigned index, std::uint8_t value) {
    const auto shift = (1 - (index & 1)) * 8;
    lanes[index >> 1] = static_cast<std::uint16_t>((lanes[index >> 1] & ~(255u << shift)) |
                                                   (unsigned(value) << shift));
  }
};

struct RspState {
  std::array<std::uint32_t, 32> gpr{};
  std::array<RspVector, 32> vectors{};
  std::array<std::uint64_t, 8> accumulator{};
  std::uint8_t carry_low = 0;
  std::uint8_t carry_high = 0;
  std::uint8_t compare_low = 0;
  std::uint8_t compare_high = 0;
  std::uint8_t extension = 0;
  std::uint16_t divide_input = 0;
  std::uint16_t divide_output = 0;
  bool divide_double = false;
};

} // namespace cupid::n64
