#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::hi_lo(Cpu *cpu, sljit_s32 encoded) {
  const auto instruction = static_cast<std::uint32_t>(encoded);
  const auto function = instruction & 63;
  const auto a = cpu->state_.gpr[(instruction >> 21) & 31];
  const auto b = cpu->state_.gpr[(instruction >> 16) & 31];
  if (function & 2)
    cpu->divide(a, b, function >= 28, !(function & 1));
  else
    cpu->multiply(a, b, function >= 28, !(function & 1));
}

bool CpuCompiler::Emitter::arithmetic(std::uint32_t instruction, bool full, bool defer_exit,
                                      bool delay) {
  const auto opcode = instruction >> 26;
  const auto function = instruction & 63;
  const bool hilo = opcode == 0 && function >= 24 && function <= 31;
  const bool trap =
      opcode == 8 || opcode == 24 ||
      (opcode == 0 && (function == 32 || function == 34 || function == 44 || function == 46));
  const bool dual = opcode == 24 || (opcode == 0 && function >= (hilo ? 28u : 44u));
  if ((!hilo && !trap) || (dual && !wide))
    return false;
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const unsigned latency = hilo ? (function & 2) ? dual ? 136 : 72 : dual ? 14 : 8 : 0;
  commit_pipeline();
  SlowPath path{{}, nullptr, instruction, cycles, delay ? 0u : latency + 2};
  if (hilo) {
    if (function & 2) {
      const auto denominator = gpr(target);
      path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_EQUAL | (function == 27 ? SLJIT_32 : 0),
                                          denominator.type, denominator.value, SLJIT_IMM, 0));
    }
    op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
    op1(SLJIT_MOV32, reg(SLJIT_R1), imm(instruction));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, 32), SLJIT_IMM, SLJIT_FUNC_ADDR(hi_lo));
  } else {
    const auto immediate = static_cast<std::uint64_t>(
        std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
    const bool subtract = opcode == 0 && (function == 34 || function == 46);
    op2((subtract ? SLJIT_SUB : SLJIT_ADD) | SLJIT_SET_OVERFLOW | (dual ? 0 : SLJIT_32),
        reg(SLJIT_R0), gpr(source), opcode == 0 ? gpr(target) : imm(immediate));
    path.enter.push_back(sljit_emit_jump(compiler, SLJIT_OVERFLOW));
    store(opcode == 0 ? (instruction >> 11) & 31 : target, reg(SLJIT_R0), !dual);
  }
  cycles += latency;
  if (full) {
    begin(false);
    end(defer_exit);
  } else {
    cycles += 2;
    pipeline_dirty = true;
  }
  if (!path.enter.empty())
    finish_slow_path(std::move(path), delay);
  return true;
}

} // namespace cupid::n64
