#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

bool RspCompiler::Emitter::integer(std::uint32_t instruction) {
  const auto opcode = instruction >> 26;
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto rd = (instruction >> 11) & 31;
  const auto shift = (instruction >> 6) & 31;
  const auto immediate = static_cast<std::uint32_t>(
      std::int32_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  if (opcode == 0) {
    if (!rd)
      return true;
    switch (instruction & 63) {
    case 0:
    case 2:
    case 3:
      op2((instruction & 63) == 0   ? SLJIT_SHL32
          : (instruction & 63) == 2 ? SLJIT_LSHR32
                                    : SLJIT_ASHR32,
          reg(SLJIT_R0), gpr(rt), imm(shift));
      store(rd, reg(SLJIT_R0));
      break;
    case 4:
    case 6:
    case 7:
      op2((instruction & 63) == 4   ? SLJIT_MSHL32
          : (instruction & 63) == 6 ? SLJIT_MLSHR32
                                    : SLJIT_MASHR32,
          reg(SLJIT_R0), gpr(rt), gpr(rs));
      store(rd, reg(SLJIT_R0));
      break;
    case 32:
    case 33:
    case 34:
    case 35:
      op2((instruction & 63) <= 33 ? SLJIT_ADD32 : SLJIT_SUB32, reg(SLJIT_R0), gpr(rs), gpr(rt));
      store(rd, reg(SLJIT_R0));
      break;
    case 36:
    case 37:
    case 38:
    case 39:
      op2((instruction & 63) == 36   ? SLJIT_AND32
          : (instruction & 63) == 38 ? SLJIT_XOR32
                                     : SLJIT_OR32,
          reg(SLJIT_R0), gpr(rs), gpr(rt));
      if ((instruction & 63) == 39)
        op2(SLJIT_XOR32, reg(SLJIT_R0), reg(SLJIT_R0), imm(0xffffffff));
      store(rd, reg(SLJIT_R0));
      break;
    case 42:
    case 43:
      compare(rd, gpr(rs), gpr(rt), (instruction & 63) == 42);
      break;
    default:
      return false;
    }
  } else {
    if (opcode >= 8 && opcode <= 15 && !rt)
      return true;
    switch (opcode) {
    case 8:
    case 9:
      op2(SLJIT_ADD32, reg(SLJIT_R0), gpr(rs), imm(immediate));
      store(rt, reg(SLJIT_R0));
      break;
    case 10:
    case 11:
      compare(rt, gpr(rs), imm(immediate), opcode == 10);
      break;
    case 12:
    case 13:
    case 14:
      op2(opcode == 12   ? SLJIT_AND32
          : opcode == 13 ? SLJIT_OR32
                         : SLJIT_XOR32,
          reg(SLJIT_R0), gpr(rs), imm(instruction & 0xffff));
      store(rt, reg(SLJIT_R0));
      break;
    case 15:
      store(rt, imm(instruction << 16));
      break;
    default:
      return false;
    }
  }
  return true;
}

} // namespace cupid::n64
