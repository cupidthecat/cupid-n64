#include "core/cpu/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

bool CpuCompiler::Emitter::branch(std::uint32_t instruction) {
  if (!block_instruction(instruction).branch)
    return false;
  const auto opcode = instruction >> 26;
  if (opcode == 17 && !(cpu.control_[Status] & 0x20000000))
    return false;
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto function = instruction & 63;
  const auto offset = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction));

  begin();
  if (opcode == 0) {
    op1(SLJIT_MOV, reg(SLJIT_R0), gpr(rs));
    if (function == 9) {
      op2(SLJIT_ADD, reg(SLJIT_R1), field(&cpu.pipeline_pc_), imm(4));
      op1(SLJIT_MOV, gpr((instruction >> 11) & 31), reg(SLJIT_R1));
    }
    op1(SLJIT_MOV, field(&cpu.next_pc_), reg(SLJIT_R0));
    op1(SLJIT_MOV_U8, field(&cpu.next_delay_slot_), imm(1));
    op1(SLJIT_MOV_U8, field(&cpu.next_block_exit_), imm(1));
  } else if (opcode == 2 || opcode == 3) {
    if (opcode == 3)
      op2(SLJIT_ADD, gpr(31), field(&cpu.pipeline_pc_), imm(4));
    op2(SLJIT_AND, reg(SLJIT_R0), field(&cpu.pipeline_pc_), imm(~0x0fffffffull));
    op2(SLJIT_OR, field(&cpu.next_pc_), reg(SLJIT_R0),
        imm(std::uint64_t(instruction & 0x03ffffff) << 2));
    op1(SLJIT_MOV_U8, field(&cpu.next_delay_slot_), imm(1));
    op1(SLJIT_MOV_U8, field(&cpu.next_block_exit_), imm(1));
  } else {
    auto left = gpr(rs);
    auto right = imm(0);
    sljit_s32 taken;
    bool likely;
    if (opcode == 17) {
      op2(SLJIT_AND | SLJIT_32, state(offsetof(CpuState, fcr31)), state(offsetof(CpuState, fcr31)),
          imm(~0x0003f000u));
      op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R2), state(offsetof(CpuState, fcr31)), imm(0x800000));
      left = reg(SLJIT_R2);
      taken = (rt & 1) ? SLJIT_NOT_EQUAL : SLJIT_EQUAL;
      likely = rt & 2;
    } else if (opcode == 1) {
      if (rt >= 16) {
        if (rt == 17)
          op1(SLJIT_MOV, reg(SLJIT_R2), left);
        op2(SLJIT_ADD, reg(SLJIT_R0), field(&cpu.pipeline_pc_), imm(4));
        store(31, reg(SLJIT_R0), true);
        if (rt == 17)
          left = reg(SLJIT_R2);
      }
      taken = (rt & 1) ? SLJIT_SIG_GREATER_EQUAL : SLJIT_SIG_LESS;
      likely = rt & 2;
    } else {
      const auto condition = opcode & 3;
      taken = condition == 0   ? SLJIT_EQUAL
              : condition == 1 ? SLJIT_NOT_EQUAL
              : condition == 2 ? SLJIT_SIG_LESS_EQUAL
                               : SLJIT_SIG_GREATER;
      if (condition <= 1)
        right = gpr(rt);
      likely = opcode >= 20;
    }
    const auto take =
        sljit_emit_cmp(compiler, taken, left.type, left.value, right.type, right.value);
    if (likely) {
      op2(SLJIT_ADD, reg(SLJIT_R0), field(&cpu.pipeline_pc_), imm(4));
      op1(SLJIT_MOV, field(&cpu.pipeline_pc_), reg(SLJIT_R0));
      op2(SLJIT_ADD, field(&cpu.next_pc_), reg(SLJIT_R0), imm(4));
      op1(SLJIT_MOV, reg(SLJIT_S3), imm(1));
    } else {
      op1(SLJIT_MOV_U8, field(&cpu.next_delay_slot_), imm(1));
    }
    const auto done = sljit_emit_jump(compiler, SLJIT_JUMP);
    sljit_set_label(take, sljit_emit_label(compiler));
    op2(SLJIT_ADD, field(&cpu.next_pc_), field(&cpu.pipeline_pc_),
        imm(static_cast<std::uint64_t>(std::int64_t(offset) * 4)));
    op1(SLJIT_MOV_U8, field(&cpu.next_delay_slot_), imm(1));
    op1(SLJIT_MOV_U8, field(&cpu.next_block_exit_), imm(1));
    sljit_set_label(done, sljit_emit_label(compiler));
  }
  const auto self_jump = (2u << 26) | static_cast<std::uint32_t>((pc >> 2) & 0x03ffffff);
  if (instruction == 0x1000ffff || instruction == self_jump)
    advance(126);
  op1(SLJIT_MOV, gpr(0), imm(0));
  op1(SLJIT_MOV, state(offsetof(CpuState, pc)), field(&cpu.pipeline_pc_));
  op1(SLJIT_MOV_U8, reg(SLJIT_R0), field(&cpu.next_delay_slot_));
  op1(SLJIT_MOV_U8, field(&cpu.delay_slot_), reg(SLJIT_R0));
  op1(SLJIT_MOV_U8, reg(SLJIT_R0), field(&cpu.next_block_exit_));
  op1(SLJIT_MOV_U8, field(&cpu.block_exit_), reg(SLJIT_R0));
  return_if(SLJIT_NOT_EQUAL, reg(SLJIT_S3), imm(0), 0);
  return true;
}

std::optional<std::uint64_t> CpuCompiler::Emitter::branch_target(unsigned branch) const {
  const auto instruction = block.words[branch];
  const auto info = block_instruction(instruction);
  if (!info.branch)
    return {};
  const auto opcode = instruction >> 26;
  const auto address = start_pc + branch * 4;
  if (opcode == 2 || opcode == 3) {
    return ((address + 4) & ~0x0fffffffull) | (std::uint64_t(instruction & 0x03ffffff) << 2);
  } else if (opcode == 1 || (opcode >= 4 && opcode <= 7) || (opcode >= 20 && opcode <= 23) ||
             opcode == 17) {
    const auto offset = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction));
    return address + 4 + static_cast<std::uint64_t>(std::int64_t(offset) * 4);
  }
  return {};
}

std::optional<unsigned> CpuCompiler::Emitter::entry_index(std::uint64_t target) const {
  if (target < start_pc || target - start_pc >= block.words.size() * 4 || (target & 3))
    return {};
  const auto index = static_cast<unsigned>((target - start_pc) >> 2);
  if (index && block_instruction(block.words[index - 1]).branch)
    return {};
  return index;
}

void CpuCompiler::Emitter::plan_entries() {
  internal_entries.resize(block.words.size());
  instruction_labels.resize(block.words.size());
  const auto add = [&](std::uint64_t address) {
    if (const auto index = entry_index(address))
      internal_entries[*index] = true;
  };
  for (unsigned n = 0; n < block.words.size(); ++n) {
    const auto instruction = block.words[n];
    const auto info = block_instruction(instruction);
    if (!info.branch)
      continue;
    const auto opcode = instruction >> 26;
    const bool link = opcode == 3 || (opcode == 0 && (instruction & 63) == 9);
    if (link || !info.stop_after_delay)
      add(start_pc + n * 4 + 8);
    if (!info.stop_after_delay && !link)
      if (const auto target = branch_target(n))
        add(*target);
    if (const auto target = internal_target(n))
      internal_entries[*target] = true;
  }
  for (unsigned n = 1; n < internal_entries.size(); ++n)
    if (internal_entries[n])
      block.entries.push_back(n);
}

std::optional<unsigned> CpuCompiler::Emitter::internal_target(unsigned branch) const {
  if (branch + 1 >= block.words.size())
    return {};
  const auto delay = block_instruction(block.words[branch + 1]);
  if (delay.branch || delay.terminal)
    return {};
  const auto target = branch_target(branch);
  if (!target)
    return {};
  const auto entry = entry_index(*target);
  if (!entry)
    return {};
  // Keep repeated fetches in one cache line when guest stores change the backing memory.
  if ((*target & ~31ull) != ((start_pc + branch * 4 + 4) & ~31ull))
    return {};
  const auto index = *entry;
  for (unsigned n = std::min(index, branch); n <= std::max(index, branch + 1); ++n)
    if ((block.words[n] >> 26) == 47)
      return {};
  return index;
}

sljit_sw CpuCompiler::Emitter::loop_pending(Cpu *cpu) {
  return cpu->nmi_pending_ || cpu->bus_.frozen() ||
         ((cpu->control_[Status] & 7) == 1 &&
          (cpu->control_[Status] & cpu->control_[Cause] & 0xff00));
}

void CpuCompiler::Emitter::dispatch_internal(unsigned target) {
  op2(SLJIT_AND, reg(SLJIT_R0), reg(SLJIT_S3), imm(2));
  return_if(SLJIT_NOT_EQUAL, reg(SLJIT_R0), imm(0), 0);
  const auto fallthrough = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_S3, 0, SLJIT_IMM, 0);
  return_if(SLJIT_GREATER_EQUAL, state(offsetof(CpuState, clocks)), {SLJIT_MEM1(SLJIT_S0)}, 0);
  return_if(SLJIT_NOT_EQUAL, state(offsetof(CpuState, pc)), imm(start_pc + target * 4), 0);
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(W, P), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(loop_pending));
  return_if(SLJIT_NOT_EQUAL, reg(SLJIT_R0), imm(0), 0);
  internal_jumps.emplace_back(sljit_emit_jump(compiler, SLJIT_JUMP), target);
  sljit_set_label(fallthrough, sljit_emit_label(compiler));
}

} // namespace cupid::n64
