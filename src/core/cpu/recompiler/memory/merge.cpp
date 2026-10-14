#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::merge(std::uint32_t instruction) {
  const auto opcode = instruction >> 26;
  const unsigned bytes = opcode == 26 || opcode == 27 || opcode == 44 || opcode == 45 ? 8 : 4;
  const bool left = opcode == 26 || opcode == 34 || opcode == 42 || opcode == 44;
  const bool write = opcode >= 40;
  const auto target = (instruction >> 16) & 31;
  if (!write && !target)
    return;
  if (write)
    op1(SLJIT_MOV_U8, {SLJIT_MEM1(SLJIT_R2), offsetof(Cpu::CacheLine, dirty)}, imm(1));
  op2(SLJIT_AND, reg(SLJIT_R0), reg(SLJIT_R1), imm(16 - bytes));
  op2(SLJIT_ADD, reg(SLJIT_R2), reg(SLJIT_R2), reg(SLJIT_R0));
  op2(SLJIT_AND, reg(SLJIT_R1), reg(SLJIT_R1), imm(bytes - 1));
  if (cpu.reverse_endian() != !left)
    op2(SLJIT_XOR, reg(SLJIT_R1), reg(SLJIT_R1), imm(bytes - 1));
  op2(SLJIT_SHL, reg(SLJIT_R1), reg(SLJIT_R1), imm(3));
  const Operand data{SLJIT_MEM1(SLJIT_R2), offsetof(Cpu::CacheLine, words)};
  op1(SLJIT_MOV_U32, reg(SLJIT_R3), data);
  if (bytes == 8) {
    op1(SLJIT_MOV_U32, reg(SLJIT_R0), {data.type, data.value + 4});
    op2(SLJIT_SHL, reg(SLJIT_R3), reg(SLJIT_R3), imm(32));
    op2(SLJIT_OR, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R0));
  }
  const auto width_mask = bytes == 8 ? ~0ull : 0xffffffffull;
  const auto direction = left != write ? SLJIT_SHL : SLJIT_LSHR;
  op2(direction, reg(SLJIT_R0), imm(width_mask), reg(SLJIT_R1));
  if (write) {
    op2(direction, reg(SLJIT_R4), gpr(target), reg(SLJIT_R1));
    op2(SLJIT_AND, reg(SLJIT_R4), reg(SLJIT_R4), reg(SLJIT_R0));
    op2(SLJIT_XOR, reg(SLJIT_R0), reg(SLJIT_R0), imm(~0ull));
    op2(SLJIT_AND, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R0));
    op2(SLJIT_OR, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R4));
    if (bytes == 8) {
      op2(SLJIT_LSHR, reg(SLJIT_R0), reg(SLJIT_R3), imm(32));
      op1(SLJIT_MOV32, data, reg(SLJIT_R0));
      op1(SLJIT_MOV32, {data.type, data.value + 4}, reg(SLJIT_R3));
    } else {
      op1(SLJIT_MOV32, data, reg(SLJIT_R3));
    }
  } else {
    op2(direction, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R1));
    op2(SLJIT_AND, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R0));
    op2(SLJIT_XOR, reg(SLJIT_R0), reg(SLJIT_R0), imm(~0ull));
    op2(SLJIT_AND, reg(SLJIT_R4), gpr(target), reg(SLJIT_R0));
    op2(SLJIT_OR, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R4));
    if (bytes == 4) {
      auto *partial_right =
          left ? nullptr : sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
      op1(SLJIT_MOV_S32, reg(SLJIT_R3), reg(SLJIT_R3));
      if (partial_right)
        sljit_set_label(partial_right, sljit_emit_label(compiler));
    }
    store(target, reg(SLJIT_R3));
  }
}

void CpuCompiler::Emitter::cached_merge(Cpu *cpu, sljit_s32 encoded, Cpu::CacheLine *line,
                                        sljit_uw physical) {
  const auto instruction = static_cast<std::uint32_t>(encoded);
  const auto opcode = instruction >> 26;
  const unsigned bytes = opcode == 26 || opcode == 27 || opcode == 44 || opcode == 45 ? 8 : 4;
  const bool left = opcode == 26 || opcode == 34 || opcode == 42 || opcode == 44;
  const bool write = opcode >= 40;
  const auto target = (instruction >> 16) & 31;
  const auto offset = static_cast<std::uint64_t>(
      std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  const auto address = cpu->state_.gpr[(instruction >> 21) & 31] + offset;
  const unsigned lane =
      cpu->reverse_endian() ? bytes - 1 - (address & (bytes - 1)) : address & (bytes - 1);
  const unsigned shift = (left ? lane : bytes - 1 - lane) * 8;
  const auto word = (physical & (16 - bytes)) >> 2;
  const std::uint64_t memory =
      bytes == 8 ? (std::uint64_t(line->words[word]) << 32) | line->words[word + 1]
                 : line->words[word];
  const std::uint64_t width_mask = bytes == 8 ? ~0ull : 0xffffffffull;
  const auto value = cpu->state_.gpr[target];
  if (write) {
    const auto mask = (left ? width_mask >> shift : width_mask << shift) & width_mask;
    if (cpu->diagnostics_ && cpu->diagnostics_->instrumented()) {
      std::uint16_t dirty = 0;
      unsigned length = 0;
      for (unsigned n = 0; n < bytes; ++n)
        if ((mask >> ((bytes - n - 1) * 8)) & 255) {
          dirty |= static_cast<std::uint16_t>(1u << ((physical & (16 - bytes)) + n));
          ++length;
        }
      for (unsigned n = 0; n < static_cast<unsigned>(std::popcount(length)); ++n)
        cpu->diagnostics_->cache_access(false, true);
      cpu->diagnostics_->cache_dirty(static_cast<unsigned>(line - cpu->dcache_.data()),
                                     static_cast<std::uint32_t>(physical), dirty, cpu->state_.pc);
    }
    const auto inserted = left ? value >> shift : value << shift;
    const auto result = (memory & ~mask) | (inserted & mask);
    if (bytes == 8) {
      line->words[word] = static_cast<std::uint32_t>(result >> 32);
      line->words[word + 1] = static_cast<std::uint32_t>(result);
    } else
      line->words[word] = static_cast<std::uint32_t>(result);
    line->dirty = true;
  } else {
    if (cpu->diagnostics_)
      cpu->diagnostics_->cache_access(false, true);
    if (!target)
      return;
    const auto mask = left ? width_mask << shift : width_mask >> shift;
    const auto inserted = left ? memory << shift : memory >> shift;
    const auto result = (value & ~mask) | (inserted & mask);
    cpu->state_.gpr[target] =
        bytes == 4 && (left || shift == 0) ? sign_word(static_cast<std::uint32_t>(result)) : result;
  }
}

} // namespace cupid::n64
