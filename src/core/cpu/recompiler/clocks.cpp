#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::step(Cpu *cpu, sljit_uw clocks) {
  cpu->advance_clocks(clocks);
}

void CpuCompiler::Emitter::advance(unsigned clocks) {
  if (!clocks)
    return;
  constexpr std::uint64_t mask = (1ull << 33) - 1;
  const auto count = field(&cpu.count_ticks_);
  op2(SLJIT_SHL, reg(SLJIT_R0), field(&cpu.control_[Compare]), imm(1));
  op2(SLJIT_SUB, reg(SLJIT_R0), reg(SLJIT_R0), count);
  op2(SLJIT_AND, reg(SLJIT_R0), reg(SLJIT_R0), imm(mask));
  if (clocks & 1) {
    op2(SLJIT_AND, reg(SLJIT_R1), state(offsetof(CpuState, clocks)), imm(1));
    op2(SLJIT_ADD, reg(SLJIT_R1), reg(SLJIT_R1), imm(clocks));
    op2(SLJIT_LSHR, reg(SLJIT_R1), reg(SLJIT_R1), imm(1));
  } else {
    op1(SLJIT_MOV, reg(SLJIT_R1), imm(clocks >> 1));
  }
  const auto equal_count = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
  const auto timer = sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL, SLJIT_R1, 0, SLJIT_R0, 0);
  sljit_set_label(equal_count, sljit_emit_label(compiler));
  op2(SLJIT_ADD, reg(SLJIT_R0), count, reg(SLJIT_R1));
  op2(SLJIT_AND, count, reg(SLJIT_R0), imm(mask));
  op2(SLJIT_ADD, state(offsetof(CpuState, clocks)), state(offsetof(CpuState, clocks)), imm(clocks));
  const auto done = sljit_emit_jump(compiler, SLJIT_JUMP);
  sljit_set_label(timer, sljit_emit_label(compiler));
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV, reg(SLJIT_R1), imm(clocks));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, W), SLJIT_IMM, SLJIT_FUNC_ADDR(step));
  sljit_set_label(done, sljit_emit_label(compiler));
}

} // namespace cupid::n64
