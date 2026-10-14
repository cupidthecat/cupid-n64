#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

void RspCompiler::Emitter::vector_transpose(std::uint32_t instruction) {
  const bool store = (instruction >> 26) == 58;
  const auto base = ((instruction >> 16) & 31) & ~7u;
  const auto element = (instruction >> 7) & 15;
  if (vector_base) {
    vector_window_base = *vector_base & 0xff8;
    vector_window_offset =
        store ? (*vector_base & 7) - (element & ~1u) : (*vector_base & 8) + element;
  } else if (store) {
    op2(SLJIT_AND32, reg(SLJIT_R2), reg(SLJIT_R1), imm(7));
    op2(SLJIT_SUB32, reg(SLJIT_R2), reg(SLJIT_R2), imm(element & ~1u));
  } else {
    op2(SLJIT_AND32, reg(SLJIT_R2), reg(SLJIT_R1), imm(8));
    op2(SLJIT_ADD32, reg(SLJIT_R2), reg(SLJIT_R2), imm(element));
  }
  if (!vector_base)
    op2(SLJIT_AND32, reg(SLJIT_R1), reg(SLJIT_R1), imm(0xff8));
  for (unsigned n = 0; n < 16; ++n) {
    const auto index = store ? base + n / 2 : base + ((element / 2 + n / 2) & 7);
    const auto byte = store ? (16 - (element & ~1u) + n) & 15 : n;
    const auto value = vector_byte(index, byte);
    const auto memory = vector_location(n, true);
    op1(SLJIT_MOV_U8, reg(SLJIT_R3), store ? value : memory);
    op1(SLJIT_MOV_U8, store ? memory : value, reg(SLJIT_R3));
  }
}

} // namespace cupid::n64
