#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::step(Cpu *cpu, sljit_uw clocks) {
  cpu->advance_clocks(clocks);
}

void CpuCompiler::Emitter::advance(unsigned clocks, bool preserve_address) {
  if (!clocks)
    return;
  op2(SLJIT_ADD, reg(SLJIT_R0), state(offsetof(CpuState, clocks)), imm(clocks));
  const auto timer =
      sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, field(&cpu.timer_deadline_).type,
                     field(&cpu.timer_deadline_).value);
  op1(SLJIT_MOV, state(offsetof(CpuState, clocks)), reg(SLJIT_R0));
  const auto done = sljit_emit_jump(compiler, SLJIT_JUMP);
  sljit_set_label(timer, sljit_emit_label(compiler));
  if (preserve_address) {
    op1(SLJIT_MOV, {SLJIT_MEM1(SLJIT_SP), 0}, reg(SLJIT_R1));
    op1(SLJIT_MOV, {SLJIT_MEM1(SLJIT_SP), sizeof(sljit_sw)}, reg(SLJIT_R2));
  }
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV, reg(SLJIT_R1), imm(clocks));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, W), SLJIT_IMM, SLJIT_FUNC_ADDR(step));
  if (preserve_address) {
    op1(SLJIT_MOV, reg(SLJIT_R1), {SLJIT_MEM1(SLJIT_SP), 0});
    op1(SLJIT_MOV, reg(SLJIT_R2), {SLJIT_MEM1(SLJIT_SP), sizeof(sljit_sw)});
  }
  sljit_set_label(done, sljit_emit_label(compiler));
}

} // namespace cupid::n64
