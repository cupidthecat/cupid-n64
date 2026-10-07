#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

void RspCompiler::Emitter::flush_clocks() {
  if (cycles)
    op2(SLJIT_ADD, field(&rsp.clock_), field(&rsp.clock_), imm(cycles));
  cycles = 0;
}

void RspCompiler::Emitter::commit_pipeline() {
  for (unsigned n = 0; n < 3; ++n) {
    const auto &stage = pipeline.previous[n];
    auto &dest = rsp.pipeline_.previous[n];
    op1(SLJIT_MOV32, field(&dest.gpr), imm(stage.gpr));
    op1(SLJIT_MOV32, field(&dest.vector), imm(stage.vector));
    op1(SLJIT_MOV_U8, field(&dest.load), imm(stage.load));
  }
  op1(SLJIT_MOV32, field(&rsp.pipeline_.clocks), imm(0));
  op1(SLJIT_MOV_U8, field(&rsp.pipeline_.single_issue), imm(pipeline.single_issue));
}

void RspCompiler::Emitter::commit(std::uint32_t pc, bool branch) {
  op1(SLJIT_MOV32, field(&rsp.pc_), imm(pc));
  op1(SLJIT_MOV32, field(&rsp.pipeline_pc_), imm(pc));
  if (branch) {
    op1(SLJIT_MOV_U8, reg(SLJIT_R0), field(&rsp.next_delay_slot_));
    op1(SLJIT_MOV_U8, field(&rsp.delay_slot_), reg(SLJIT_R0));
  } else {
    op1(SLJIT_MOV32, field(&rsp.next_pc_), imm((pc + 4) & 0xfff));
    op1(SLJIT_MOV_U8, field(&rsp.delay_slot_), imm(0));
    op1(SLJIT_MOV_U8, field(&rsp.next_delay_slot_), imm(0));
  }
}

void RspCompiler::Emitter::begin_delay() {
  op1(SLJIT_MOV32, reg(SLJIT_R0), field(&rsp.next_pc_));
  op1(SLJIT_MOV32, field(&rsp.pipeline_pc_), reg(SLJIT_R0));
  op2(SLJIT_ADD32, reg(SLJIT_R0), reg(SLJIT_R0), imm(4));
  op2(SLJIT_AND32, field(&rsp.next_pc_), reg(SLJIT_R0), imm(0xfff));
  op1(SLJIT_MOV_U8, field(&rsp.next_delay_slot_), imm(0));
}

void RspCompiler::Emitter::end_delay(Rsp *rsp) {
  rsp->end_instruction();
}

void RspCompiler::Emitter::halt_exit(std::uint32_t pc, bool branch) {
  op1(SLJIT_MOV_U8, reg(SLJIT_R0), field(&rsp.status_.halted));
  const auto jump = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
  exits.push_back({jump, pipeline, pc, cycles, branch});
}

} // namespace cupid::n64
