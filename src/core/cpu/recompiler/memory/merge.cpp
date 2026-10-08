#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

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
    const auto inserted = left ? value >> shift : value << shift;
    const auto result = (memory & ~mask) | (inserted & mask);
    if (bytes == 8) {
      line->words[word] = static_cast<std::uint32_t>(result >> 32);
      line->words[word + 1] = static_cast<std::uint32_t>(result);
    } else
      line->words[word] = static_cast<std::uint32_t>(result);
    line->dirty = true;
  } else if (target) {
    const auto mask = left ? width_mask << shift : width_mask >> shift;
    const auto inserted = left ? memory << shift : memory >> shift;
    const auto result = (value & ~mask) | (inserted & mask);
    cpu->state_.gpr[target] =
        bytes == 4 && (left || shift == 0) ? sign_word(static_cast<std::uint32_t>(result)) : result;
  }
}

} // namespace cupid::n64
