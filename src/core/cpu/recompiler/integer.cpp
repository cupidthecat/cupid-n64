#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

bool CpuCompiler::Emitter::integer(std::uint32_t instruction) {
  const auto opcode = instruction >> 26;
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto immediate = static_cast<std::uint64_t>(
      std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  switch (opcode) {
  case 0:
    return special(instruction);
  case 9:
  case 25:
    op2(SLJIT_ADD, reg(SLJIT_R0), gpr(rs), imm(immediate));
    store(rt, reg(SLJIT_R0), opcode == 9);
    return true;
  case 10:
  case 11:
    compare(reg(SLJIT_R0), gpr(rs), imm(immediate), opcode == 10);
    store(rt, reg(SLJIT_R0));
    return true;
  case 12:
  case 13:
  case 14:
    op2(opcode == 12   ? SLJIT_AND
        : opcode == 13 ? SLJIT_OR
                       : SLJIT_XOR,
        reg(SLJIT_R0), gpr(rs), imm(instruction & 0xffff));
    store(rt, reg(SLJIT_R0));
    return true;
  case 15:
    store(rt, imm(sign_word(instruction << 16)));
    return true;
  }
  return false;
}

bool CpuCompiler::Emitter::special(std::uint32_t instruction) {
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto rd = (instruction >> 11) & 31;
  const auto shift = (instruction >> 6) & 31;
  switch (instruction & 63) {
  case 0:
  case 2:
  case 3: {
    const auto function = instruction & 63;
    op2(function == 0   ? SLJIT_SHL32
        : function == 2 ? SLJIT_LSHR32
                        : SLJIT_ASHR,
        reg(SLJIT_R0), gpr(rt), imm(shift));
    store(rd, reg(SLJIT_R0), true);
    return true;
  }
  case 4:
  case 6:
  case 7: {
    const auto function = instruction & 63;
    op2(SLJIT_AND, reg(SLJIT_R1), gpr(rs), imm(31));
    op2(function == 4   ? SLJIT_MSHL32
        : function == 6 ? SLJIT_MLSHR32
                        : SLJIT_MASHR,
        reg(SLJIT_R0), gpr(rt), reg(SLJIT_R1));
    store(rd, reg(SLJIT_R0), true);
    return true;
  }
  case 15:
    return true;
  case 16:
    store(rd, state(offsetof(CpuState, hi)));
    return true;
  case 17:
    op1(SLJIT_MOV, state(offsetof(CpuState, hi)), gpr(rs));
    return true;
  case 18:
    store(rd, state(offsetof(CpuState, lo)));
    return true;
  case 19:
    op1(SLJIT_MOV, state(offsetof(CpuState, lo)), gpr(rs));
    return true;
  case 20:
  case 22:
  case 23: {
    const auto function = instruction & 63;
    op2(function == 20   ? SLJIT_MSHL
        : function == 22 ? SLJIT_MLSHR
                         : SLJIT_MASHR,
        reg(SLJIT_R0), gpr(rt), gpr(rs));
    store(rd, reg(SLJIT_R0));
    return true;
  }
  case 33:
  case 35:
  case 45:
  case 47: {
    const auto function = instruction & 63;
    op2(function == 33 || function == 45 ? SLJIT_ADD : SLJIT_SUB, reg(SLJIT_R0), gpr(rs), gpr(rt));
    store(rd, reg(SLJIT_R0), function == 33 || function == 35);
    return true;
  }
  case 36:
  case 37:
  case 38:
  case 39: {
    const auto function = instruction & 63;
    op2(function == 36   ? SLJIT_AND
        : function == 38 ? SLJIT_XOR
                         : SLJIT_OR,
        reg(SLJIT_R0), gpr(rs), gpr(rt));
    if (function == 39)
      op2(SLJIT_XOR, reg(SLJIT_R0), reg(SLJIT_R0), imm(~0ull));
    store(rd, reg(SLJIT_R0));
    return true;
  }
  case 42:
  case 43:
    compare(reg(SLJIT_R0), gpr(rs), gpr(rt), (instruction & 63) == 42);
    store(rd, reg(SLJIT_R0));
    return true;
  case 56:
  case 58:
  case 59:
  case 60:
  case 62:
  case 63: {
    const auto function = instruction & 63;
    const auto amount = shift + (function >= 60 ? 32 : 0);
    op2(function == 56 || function == 60   ? SLJIT_SHL
        : function == 58 || function == 62 ? SLJIT_LSHR
                                           : SLJIT_ASHR,
        reg(SLJIT_R0), gpr(rt), imm(amount));
    store(rd, reg(SLJIT_R0));
    return true;
  }
  }
  return false;
}

} // namespace cupid::n64
