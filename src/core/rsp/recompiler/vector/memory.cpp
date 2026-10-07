#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

RspCompiler::Emitter::Operand RspCompiler::Emitter::vector_byte(unsigned index, unsigned byte) {
#if SLJIT_LITTLE_ENDIAN
  byte ^= 1;
#endif
  return {SLJIT_MEM1(SLJIT_S0),
          static_cast<sljit_sw>(offsetof(RspState, vectors) + index * sizeof(RspVector) + byte)};
}

RspCompiler::Emitter::Operand RspCompiler::Emitter::vector_half(unsigned index, unsigned lane) {
  return {SLJIT_MEM1(SLJIT_S0), static_cast<sljit_sw>(offsetof(RspState, vectors) +
                                                      index * sizeof(RspVector) + lane * 2)};
}

void RspCompiler::Emitter::vector_address(unsigned offset, bool window) {
  op2(SLJIT_ADD32, reg(SLJIT_R0), reg(window ? SLJIT_R2 : SLJIT_R1), imm(offset));
  if (window) {
    op2(SLJIT_AND32, reg(SLJIT_R0), reg(SLJIT_R0), imm(15));
    op2(SLJIT_ADD32, reg(SLJIT_R0), reg(SLJIT_R0), reg(SLJIT_R1));
  }
  op2(SLJIT_AND32, reg(SLJIT_R0), reg(SLJIT_R0), imm(0xfff));
}

bool RspCompiler::Emitter::vector_memory(std::uint32_t instruction, std::uint32_t pc) {
  const auto opcode = instruction >> 26;
  if (opcode != 50 && opcode != 58)
    return false;
  const auto operation = (instruction >> 11) & 31;
  if (operation > 11 || (opcode == 50 && operation == 10))
    return true;
  const auto source = (instruction >> 21) & 31;
  const auto immediate = static_cast<std::int32_t>(instruction << 25) >> 25;
  constexpr unsigned scales[] = {1, 2, 4, 8, 16, 16, 8, 8, 16, 16, 16, 16};
  const auto offset = static_cast<std::uint32_t>(immediate) * scales[operation];
  op2(SLJIT_ADD32, reg(SLJIT_R1), gpr(source), imm(offset));
  op2(SLJIT_AND32, reg(SLJIT_R1), reg(SLJIT_R1), imm(0xfff));
  if (operation <= 3)
    vector_linear(instruction, pc);
  else if (operation <= 5)
    vector_quad(instruction);
  else if (operation <= 10)
    vector_packed(instruction);
  else
    vector_transpose(instruction);
  return true;
}

} // namespace cupid::n64
