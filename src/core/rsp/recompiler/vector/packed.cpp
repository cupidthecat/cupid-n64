#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

void RspCompiler::Emitter::vector_packed(std::uint32_t instruction) {
  const bool store = (instruction >> 26) == 58;
  const auto operation = (instruction >> 11) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto element = (instruction >> 7) & 15;
  if (store && (operation == 6 || operation == 7)) {
    for (unsigned n = 0; n < 8; ++n) {
      const auto index = element + n;
      const bool high = ((index & 15) < 8) == (operation == 6);
      op1(high ? SLJIT_MOV_U8 : SLJIT_MOV_U16, reg(SLJIT_R3),
          high ? vector_byte(target, (index & 7) * 2) : vector_half(target, index & 7));
      if (!high)
        op2(SLJIT_LSHR32, reg(SLJIT_R3), reg(SLJIT_R3), imm(7));
      vector_address(n);
      op1(SLJIT_MOV_U8, {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)}, reg(SLJIT_R3));
    }
    return;
  }
  op2(SLJIT_AND32, reg(SLJIT_R2), reg(SLJIT_R1), imm(7));
  op2(SLJIT_AND32, reg(SLJIT_R1), reg(SLJIT_R1), imm(0xff8));
  if (!store)
    op2(SLJIT_SUB32, reg(SLJIT_R2), reg(SLJIT_R2), imm(element));
  if (operation == 6 || operation == 7 || (!store && operation == 8)) {
    for (unsigned lane = 0; lane < 8; ++lane) {
      vector_address(lane * (operation == 8 ? 2 : 1), true);
      op1(SLJIT_MOV_U8, reg(SLJIT_R3), {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)});
      op2(SLJIT_SHL32, reg(SLJIT_R3), reg(SLJIT_R3), imm(operation == 6 ? 8 : 7));
      op1(SLJIT_MOV_U16, vector_half(target, lane), reg(SLJIT_R3));
    }
  } else if (operation == 8) {
    for (unsigned lane = 0; lane < 8; ++lane) {
      op1(SLJIT_MOV_U8, reg(SLJIT_R3), vector_byte(target, (element + lane * 2) & 15));
      op2(SLJIT_SHL32, reg(SLJIT_R3), reg(SLJIT_R3), imm(1));
      op1(SLJIT_MOV_U8, reg(SLJIT_R0), vector_byte(target, (element + lane * 2 + 1) & 15));
      op2(SLJIT_LSHR32, reg(SLJIT_R0), reg(SLJIT_R0), imm(7));
      op2(SLJIT_OR32, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R0));
      vector_address(lane * 2, true);
      op1(SLJIT_MOV_U8, {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)}, reg(SLJIT_R3));
    }
  } else if (operation == 9 && !store) {
    for (unsigned byte = element; byte < std::min(element + 8, 16u); ++byte) {
      const auto lane = byte >> 1;
      vector_address((lane & 3) * 4 + (lane >= 4 ? 8 : 0), true);
      op1(SLJIT_MOV_U8, reg(SLJIT_R3), {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)});
      op2(byte & 1 ? SLJIT_SHL32 : SLJIT_LSHR32, reg(SLJIT_R3), reg(SLJIT_R3),
          imm(byte & 1 ? 7 : 1));
      op1(SLJIT_MOV_U8, vector_byte(target, byte), reg(SLJIT_R3));
    }
  } else if (operation == 9) {
    constexpr int first[] = {0, 6, -1, -1, 1, 7, -1, -1, 4, -1, -1, 3, 5, -1, -1, 0};
    for (unsigned n = 0; n < 4; ++n) {
      if (first[element] < 0)
        op1(SLJIT_MOV32, reg(SLJIT_R3), imm(0));
      else {
        const auto lane = (unsigned(first[element]) & 4) | ((unsigned(first[element]) + n) & 3);
        op1(SLJIT_MOV_U16, reg(SLJIT_R3), vector_half(target, lane));
        op2(SLJIT_LSHR32, reg(SLJIT_R3), reg(SLJIT_R3), imm(7));
      }
      vector_address(n * 4, true);
      op1(SLJIT_MOV_U8, {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)}, reg(SLJIT_R3));
    }
  } else if (operation == 10) {
    for (unsigned n = 0; n < 16; ++n) {
      op1(SLJIT_MOV_U8, reg(SLJIT_R3), vector_byte(target, (element + n) & 15));
      vector_address(n, true);
      op1(SLJIT_MOV_U8, {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)}, reg(SLJIT_R3));
    }
  }
}

} // namespace cupid::n64
