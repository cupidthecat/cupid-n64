#pragma once

#include <cstdint>

namespace cupid::n64 {

struct MemoryInstruction {
  unsigned bytes = 0;
  bool partial = false, floating = false, linked = false, write = false, wide = false;
};

constexpr MemoryInstruction memory_instruction(std::uint32_t instruction) {
  const auto opcode = instruction >> 26;
  MemoryInstruction result;
  switch (opcode) {
  case 32:
  case 36:
  case 40:
    result.bytes = 1;
    break;
  case 33:
  case 37:
  case 41:
    result.bytes = 2;
    break;
  case 34:
  case 35:
  case 38:
  case 39:
  case 42:
  case 43:
  case 46:
  case 48:
  case 49:
  case 56:
  case 57:
    result.bytes = 4;
    break;
  case 26:
  case 27:
  case 44:
  case 45:
  case 52:
  case 55:
  case 60:
  case 63:
    result.wide = true;
    [[fallthrough]];
  case 53:
  case 61:
    result.bytes = 8;
    break;
  default:
    return result;
  }
  result.partial = opcode == 26 || opcode == 27 || opcode == 34 || opcode == 38 || opcode == 42 ||
                   opcode == 44 || opcode == 45 || opcode == 46;
  result.floating = opcode == 49 || opcode == 53 || opcode == 57 || opcode == 61;
  result.linked = opcode == 48 || opcode == 52 || opcode == 56 || opcode == 60;
  result.write = (opcode >= 40 && opcode <= 46) || opcode == 56 || opcode == 57 || opcode == 60 ||
                 opcode == 61 || opcode == 63;
  return result;
}

} // namespace cupid::n64
