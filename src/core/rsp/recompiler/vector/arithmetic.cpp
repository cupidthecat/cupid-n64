#include "core/rsp/recompiler/compiler.hpp"
#include "core/rsp/vector/execute.hpp"

namespace cupid::n64 {

bool RspCompiler::Emitter::vector_arithmetic(std::uint32_t instruction) {
  if ((instruction >> 26) != 18 || ((instruction >> 21) & 31) < 16)
    return false;
  const auto operation = instruction & 63;
  if (operation == 55 || operation == 63)
    return true;
  if (vector_arithmetic_simd(instruction))
    return true;
  const auto vector = [](unsigned index) {
    return imm(offsetof(RspState, vectors) + index * sizeof(RspVector));
  };
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S0));
  op2(SLJIT_ADD, reg(SLJIT_R1), reg(SLJIT_S0), vector((instruction >> 6) & 31));
  op2(SLJIT_ADD, reg(SLJIT_R2), reg(SLJIT_S0), vector((instruction >> 11) & 31));
  op2(SLJIT_ADD, reg(SLJIT_R3), reg(SLJIT_S0), vector((instruction >> 16) & 31));
  const auto handler = vector_handler(operation, (instruction >> 21) & 15);
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(P, P, P, P), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(handler));
  return true;
}

} // namespace cupid::n64
