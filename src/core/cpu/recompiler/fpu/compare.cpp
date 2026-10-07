#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::floating_compare(SlowPath &path) {
  const auto instruction = path.instruction;
  const bool dual = ((instruction >> 21) & 31) == 17;
  const auto narrow = dual ? 0 : SLJIT_32;
  op1(dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R0),
      fpr(cpu.fpu_source((instruction >> 11) & 31), !dual));
  floating_input(path, SLJIT_R0, dual, false);
  op1(dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R1), fpr((instruction >> 16) & 31, !dual));
  floating_input(path, SLJIT_R1, dual, false);
  sljit_emit_fcopy(compiler, SLJIT_COPY_TO_F64 | narrow, SLJIT_FR0, SLJIT_R0);
  sljit_emit_fcopy(compiler, SLJIT_COPY_TO_F64 | narrow, SLJIT_FR1, SLJIT_R1);
  floating_environment(cpu.state_.fcr31 & 3);
  const auto condition = instruction & 6;
  op1(SLJIT_MOV, reg(SLJIT_R1), imm(0));
  if (condition) {
    const auto type = condition == 2   ? SLJIT_ORDERED_EQUAL
                      : condition == 4 ? SLJIT_ORDERED_LESS
                                       : SLJIT_ORDERED_LESS_EQUAL;
    auto take = sljit_emit_fcmp(compiler, type | narrow, SLJIT_FR0, 0, SLJIT_FR1, 0);
    auto done = sljit_emit_jump(compiler, SLJIT_JUMP);
    sljit_set_label(take, sljit_emit_label(compiler));
    op1(SLJIT_MOV, reg(SLJIT_R1), imm(0x800000));
    sljit_set_label(done, sljit_emit_label(compiler));
  }
  floating_restore();
  const auto csr = state(offsetof(CpuState, fcr31));
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), csr, imm(~0x0083f000u));
  op2(SLJIT_OR | SLJIT_32, csr, reg(SLJIT_R0), reg(SLJIT_R1));
}

} // namespace cupid::n64
