#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

void RspCompiler::Emitter::vector_quad(std::uint32_t instruction) {
  if (vector_quad_simd(instruction))
    return;
  const bool store = (instruction >> 26) == 58;
  const auto operation = (instruction >> 11) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto element = (instruction >> 7) & 15;
  op2(SLJIT_AND32, reg(SLJIT_R2), reg(SLJIT_R1), imm(15));
  std::vector<sljit_jump *> done;
  if (operation == 4) {
    op2(SLJIT_SUB32, reg(SLJIT_R2), imm(16), reg(SLJIT_R2));
    for (unsigned n = 0; n < (store ? 16 : 16 - element); ++n) {
      if (n)
        done.push_back(
            sljit_emit_cmp(compiler, SLJIT_LESS_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_IMM, n));
      vector_address(n);
      const Operand memory{SLJIT_MEM2(SLJIT_S2, SLJIT_R0)};
      const auto value = vector_byte(target, (element + n) & 15);
      op1(SLJIT_MOV_U8, reg(SLJIT_R3), store ? value : memory);
      op1(SLJIT_MOV_U8, store ? memory : value, reg(SLJIT_R3));
    }
  } else {
    op2(SLJIT_AND32, reg(SLJIT_R1), reg(SLJIT_R1), imm(0xff0));
    if (!store) {
      done.push_back(
          sljit_emit_cmp(compiler, SLJIT_LESS_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_IMM, element));
      op2(SLJIT_SUB32, reg(SLJIT_R2), reg(SLJIT_R2), imm(element));
    }
    for (unsigned n = 0; n < 16; ++n) {
      done.push_back(
          sljit_emit_cmp(compiler, SLJIT_LESS_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_IMM, n));
      if (!store) {
        vector_address(n);
        op1(SLJIT_MOV_U8, reg(SLJIT_R3), {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)});
      }
      op2(SLJIT_SUB32, reg(SLJIT_R0), imm(16 + (store ? element : 0) + n), reg(SLJIT_R2));
      if (store)
        op2(SLJIT_AND32, reg(SLJIT_R0), reg(SLJIT_R0), imm(15));
#if SLJIT_LITTLE_ENDIAN
      op2(SLJIT_XOR32, reg(SLJIT_R0), reg(SLJIT_R0), imm(1));
#endif
      op2(SLJIT_ADD, reg(SLJIT_R0), reg(SLJIT_R0),
          imm(reinterpret_cast<std::uintptr_t>(&rsp.state_.vectors[target])));
      if (store) {
        op1(SLJIT_MOV_U8, reg(SLJIT_R3), {SLJIT_MEM1(SLJIT_R0)});
        vector_address(n);
        op1(SLJIT_MOV_U8, {SLJIT_MEM2(SLJIT_S2, SLJIT_R0)}, reg(SLJIT_R3));
      } else
        op1(SLJIT_MOV_U8, {SLJIT_MEM1(SLJIT_R0)}, reg(SLJIT_R3));
    }
  }
  const auto label = sljit_emit_label(compiler);
  for (const auto jump : done)
    sljit_set_label(jump, label);
}

} // namespace cupid::n64
