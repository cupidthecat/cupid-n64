#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::commit_pipeline() {
  if (!pipeline_dirty)
    return;
  op1(SLJIT_MOV, state(offsetof(CpuState, pc)), imm(pc));
  op1(SLJIT_MOV, field(&cpu.pipeline_pc_), imm(pc));
  op1(SLJIT_MOV, field(&cpu.next_pc_), imm(pc + 4));
  pipeline_dirty = false;
}

void CpuCompiler::Emitter::return_now(unsigned clocks) {
  advance(clocks);
  sljit_emit_return_void(compiler);
}

void CpuCompiler::Emitter::return_if(sljit_s32 condition, Operand left, Operand right,
                                     unsigned clocks) {
  const auto skip =
      sljit_emit_cmp(compiler, condition ^ 1, left.type, left.value, right.type, right.value);
  return_now(clocks);
  sljit_set_label(skip, sljit_emit_label(compiler));
}

void CpuCompiler::Emitter::begin() {
  commit_pipeline();
  advance(cycles);
  cycles = 0;
  op1(SLJIT_MOV_U8, field(&cpu.next_delay_slot_), imm(0));
  op1(SLJIT_MOV_U8, field(&cpu.next_block_exit_), imm(0));
  op1(SLJIT_MOV, reg(SLJIT_R0), field(&cpu.next_pc_));
  op1(SLJIT_MOV, field(&cpu.pipeline_pc_), reg(SLJIT_R0));
  op2(SLJIT_ADD, field(&cpu.next_pc_), reg(SLJIT_R0), imm(4));
  op1(SLJIT_MOV_U8, reg(SLJIT_S3), field(&cpu.block_exit_));
}

void CpuCompiler::Emitter::end(bool defer_exit) {
  op1(SLJIT_MOV, gpr(0), imm(0));
  op1(SLJIT_MOV, state(offsetof(CpuState, pc)), field(&cpu.pipeline_pc_));
  op1(SLJIT_MOV_U8, field(&cpu.delay_slot_), imm(0));
  op1(SLJIT_MOV_U8, field(&cpu.block_exit_), imm(0));
  if (!defer_exit)
    return_if(SLJIT_NOT_EQUAL, reg(SLJIT_S3), imm(0), cycles);
}

} // namespace cupid::n64
