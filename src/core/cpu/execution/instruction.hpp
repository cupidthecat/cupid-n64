#pragma once

#include <cstdint>

namespace cupid::n64 {

struct BlockInstruction {
  bool branch = false;
  bool stop_after_delay = false;
  bool terminal = false;
};

constexpr bool native_control_noop(std::uint32_t instruction) {
  const auto opcode = instruction >> 26;
  const auto format = (instruction >> 21) & 31;
  return (opcode == 16 && (format == 2 || format == 6 || format == 8)) ||
         (opcode == 18 && format >= 16);
}

constexpr BlockInstruction block_instruction(std::uint32_t instruction) {
  const auto opcode = instruction >> 26;
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto rd = (instruction >> 11) & 31;
  const auto function = instruction & 63;
  const auto special_register = [](unsigned reg) { return reg == 28 || reg == 29; };
  BlockInstruction result;
  switch (opcode) {
  case 0:
    if (function == 8 || function == 9) {
      result.branch = true;
      result.stop_after_delay = function == 8;
    }
    switch (function) {
    case 0:
    case 2:
    case 3:
    case 4:
    case 6:
    case 7:
    case 9:
    case 16:
    case 18:
    case 20:
    case 22:
    case 23:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 56:
    case 58:
    case 59:
    case 60:
    case 62:
    case 63:
      result.terminal = special_register(rd);
    }
    break;
  case 1:
    result.branch = rt <= 3 || (rt >= 16 && rt <= 19);
    break;
  case 2:
  case 3:
    result.branch = true;
    result.stop_after_delay = opcode == 2;
    break;
  case 4:
  case 5:
  case 6:
  case 7:
  case 20:
  case 21:
  case 22:
  case 23:
    result.branch = true;
    break;
  case 9:
  case 25:
    result.terminal = rt == 28 || (rt == 29 && rs != 29);
    break;
  case 8:
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
  case 15:
  case 24:
  case 26:
  case 27:
  case 32:
  case 33:
  case 34:
  case 35:
  case 36:
  case 37:
  case 38:
  case 39:
  case 48:
  case 52:
  case 55:
  case 56:
  case 60:
    result.terminal = special_register(rt);
    break;
  case 16:
    result.terminal =
        rs == 4 || rs == 5 || (rs >= 16 && function == 24) || (rs <= 1 && special_register(rt));
    break;
  case 17:
    result.branch = rs == 8 && rt <= 3;
    result.terminal = rs == 6 || (rs <= 2 && special_register(rt));
    break;
  case 18:
    result.terminal = rs <= 2 && special_register((instruction >> 15) & 31);
    break;
  }
  return result;
}

} // namespace cupid::n64
