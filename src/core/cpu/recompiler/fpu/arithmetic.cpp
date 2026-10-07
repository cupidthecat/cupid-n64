#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::floating_arithmetic(SlowPath &path) {
  const auto instruction = path.instruction;
  const auto operation = instruction & 63;
  const bool dual = ((instruction >> 21) & 31) == 17;
  const auto narrow = dual ? 0 : SLJIT_32;
  const auto dest = (instruction >> 6) & 31;
  op1(dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R0),
      fpr(cpu.fpu_source((instruction >> 11) & 31), !dual));
  floating_input(path, SLJIT_R0, dual);
  if (operation <= 3) {
    op1(dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R1), fpr((instruction >> 16) & 31, !dual));
    floating_input(path, SLJIT_R1, dual);
  }
  sljit_emit_fcopy(compiler, SLJIT_COPY_TO_F64 | narrow, SLJIT_FR0, SLJIT_R0);
  if (operation <= 3)
    sljit_emit_fcopy(compiler, SLJIT_COPY_TO_F64 | narrow, SLJIT_FR1, SLJIT_R1);
  floating_environment(cpu.state_.fcr31 & 3);
  if (operation <= 3) {
    constexpr sljit_s32 operations[]{SLJIT_ADD_F64, SLJIT_SUB_F64, SLJIT_MUL_F64, SLJIT_DIV_F64};
    sljit_emit_fop2(compiler, operations[operation] | narrow, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1,
                    0);
  } else if (operation == 4) {
    floating_sqrt(dual);
  } else {
    sljit_emit_fop1(compiler, (operation == 5 ? SLJIT_ABS_F64 : SLJIT_NEG_F64) | narrow, SLJIT_FR0,
                    0, SLJIT_FR0, 0);
  }
  sljit_emit_fcopy(compiler, SLJIT_COPY_FROM_F64 | narrow, SLJIT_FR0, SLJIT_R1);
  floating_restore();
  floating_flags(path, dual, true);
  if (!dual)
    op1(SLJIT_MOV_U32, reg(SLJIT_R1), reg(SLJIT_R1));
  op1(SLJIT_MOV, fpr(dest), reg(SLJIT_R1));
  const unsigned latency = operation <= 1   ? 4
                           : operation == 2 ? (dual ? 14 : 8)
                           : operation <= 4 ? (dual ? 114 : 56)
                                            : 0;
  advance(latency);
}

} // namespace cupid::n64
