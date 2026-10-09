#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::floating_compare(SlowPath &path) {
  const auto instruction = path.instruction;
  const bool dual = ((instruction >> 21) & 31) == 17;
  const auto narrow = dual ? 0 : SLJIT_32;
  const bool trap = cpu.state_.fcr31 & 0x800;
  std::vector<sljit_jump *> unordered;
  auto input = [&](sljit_s32 source) {
    if (trap) {
      floating_input(path, source, dual, false);
      return;
    }
    op2(SLJIT_SHL | narrow, reg(SLJIT_R2), reg(source), imm(1));
    unordered.push_back(sljit_emit_cmp(compiler, SLJIT_GREATER | narrow, SLJIT_R2, 0, SLJIT_IMM,
                                       dual ? sljit_sw(0xffe0000000000000ull) : 0xff000000));
  };
  op1(dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R0),
      fpr(cpu.fpu_source((instruction >> 11) & 31), !dual));
  op1(dual ? SLJIT_MOV : SLJIT_MOV_U32, reg(SLJIT_R1), fpr((instruction >> 16) & 31, !dual));
  input(SLJIT_R0);
  input(SLJIT_R1);
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
  if (!trap) {
    const auto ordered = sljit_emit_jump(compiler, SLJIT_JUMP);
    const auto nan = sljit_emit_label(compiler);
    for (auto jump : unordered)
      sljit_set_label(jump, nan);
    const auto result = (instruction & 1) ? 0x800000 : 0;
    if (instruction & 8) {
      op1(SLJIT_MOV, reg(SLJIT_R1), imm(result | 0x10040));
    } else {
      std::array<sljit_jump *, 2> signaling;
      for (unsigned n = 0; n < signaling.size(); ++n) {
        op2(SLJIT_SHL | narrow, reg(SLJIT_R2), reg(n ? SLJIT_R1 : SLJIT_R0), imm(1));
        signaling[n] =
            sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL | narrow, SLJIT_R2, 0, SLJIT_IMM,
                           dual ? sljit_sw(0xfff0000000000000ull) : 0xff800000);
      }
      op1(SLJIT_MOV, reg(SLJIT_R1), imm(result));
      const auto quiet = sljit_emit_jump(compiler, SLJIT_JUMP);
      const auto invalid = sljit_emit_label(compiler);
      for (auto jump : signaling)
        sljit_set_label(jump, invalid);
      op1(SLJIT_MOV, reg(SLJIT_R1), imm(result | 0x10040));
      sljit_set_label(quiet, sljit_emit_label(compiler));
    }
    sljit_set_label(ordered, sljit_emit_label(compiler));
  }
  const auto csr = state(offsetof(CpuState, fcr31));
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), csr, imm(~0x0083f000u));
  op2(SLJIT_OR | SLJIT_32, csr, reg(SLJIT_R0), reg(SLJIT_R1));
  cycles += 4;
}

} // namespace cupid::n64
