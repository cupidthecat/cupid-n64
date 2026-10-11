#pragma once

#include "core/rsp/state.hpp"
#include <bit>

namespace cupid::n64::divide {

std::uint32_t value(std::int32_t input, bool square_root);

template <int Operation = -1>
void execute_lane(RspState &state, std::uint16_t &dest, std::uint16_t selected,
                  std::uint16_t input_lane, unsigned dynamic_operation = 0) {
  const auto operation = Operation < 0 ? dynamic_operation : unsigned(Operation);
  if (operation == 0x33) {
    dest = selected;
    return;
  }
  if (operation == 0x32 || operation == 0x36) {
    state.divide_double = true;
    state.divide_input = input_lane;
    dest = state.divide_output;
    return;
  }
  const bool low = operation == 0x31 || operation == 0x35;
  const auto input =
      low && state.divide_double
          ? std::bit_cast<std::int32_t>((std::uint32_t(state.divide_input) << 16) | input_lane)
          : static_cast<std::int32_t>(static_cast<std::int16_t>(input_lane));
  const auto result = value(input, operation == 0x34 || operation == 0x35);
  state.divide_double = false;
  state.divide_output = static_cast<std::uint16_t>(result >> 16);
  dest = static_cast<std::uint16_t>(result);
}

} // namespace cupid::n64::divide
