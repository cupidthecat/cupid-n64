#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::floating_convert(SlowPath &path) {
  const auto instruction = path.instruction;
  const auto format = (instruction >> 21) & 31;
  const auto operation = instruction & 63;
  const bool source_integer = format >= 20;
  const bool source_dual = format & 1;
  const bool dest_integer = operation != 0x20 && operation != 0x21;
  const bool dest_dual = dest_integer ? operation < 12 || operation == 0x25 : operation == 0x21;
  const auto source_narrow = source_dual ? 0 : SLJIT_32;
  const auto dest_narrow = dest_dual ? 0 : SLJIT_32;
  op1(source_dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R0),
      fpr(cpu.fpu_source((instruction >> 11) & 31), !source_dual));
  if (!source_integer) {
    floating_input(path, SLJIT_R0, source_dual);
    if (dest_integer) {
      const auto limit = dest_dual ? (source_dual ? 0x4340000000000000ull : 0x5a000000ull)
                                   : (source_dual ? 0x41e0000000000000ull : 0x4f000000ull);
      op2(SLJIT_AND, reg(SLJIT_R2), reg(SLJIT_R0),
          imm(source_dual ? 0x7fffffffffffffffull : 0x7fffffffull));
      path.enter.push_back(sljit_emit_cmp(compiler, dest_dual ? SLJIT_GREATER_EQUAL : SLJIT_GREATER,
                                          SLJIT_R2, 0, SLJIT_IMM, static_cast<sljit_sw>(limit)));
      if (!dest_dual)
        path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM,
                                            static_cast<sljit_sw>(limit)));
    }
    sljit_emit_fcopy(compiler, SLJIT_COPY_TO_F64 | source_narrow, SLJIT_FR0, SLJIT_R0);
  } else if (source_dual) {
    path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_SIG_GREATER_EQUAL, SLJIT_R0, 0, SLJIT_IMM,
                                        0x0080000000000000ll));
    path.enter.push_back(
        sljit_emit_cmp(compiler, SLJIT_SIG_LESS, SLJIT_R0, 0, SLJIT_IMM, -0x0080000000000000ll));
  }
  const unsigned rounding =
      operation >= 8 && operation <= 15 ? operation & 3 : cpu.state_.fcr31 & 3;
  floating_environment(rounding);
  if (dest_integer) {
    floating_to_integer(source_dual, dest_dual);
  } else {
    const auto convert =
        source_integer
            ? (source_dual ? SLJIT_CONV_F64_FROM_SW : SLJIT_CONV_F64_FROM_S32) | dest_narrow
            : (source_dual ? SLJIT_CONV_F32_FROM_F64 : SLJIT_CONV_F64_FROM_F32);
    sljit_emit_fop1(compiler, convert, SLJIT_FR0, 0, source_integer ? SLJIT_R0 : SLJIT_FR0, 0);
    sljit_emit_fcopy(compiler, SLJIT_COPY_FROM_F64 | dest_narrow, SLJIT_FR0, SLJIT_R1);
  }
  floating_restore();
  floating_flags(path, dest_dual, !source_integer && !dest_integer);
  if (!dest_dual)
    op1(SLJIT_MOV_U32, reg(SLJIT_R1), reg(SLJIT_R1));
  op1(SLJIT_MOV, fpr((instruction >> 6) & 31), reg(SLJIT_R1));
  cycles += source_integer || dest_integer ? 8 : source_dual ? 2 : 0;
}

} // namespace cupid::n64
