#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

bool CpuCompiler::Emitter::trap(std::uint32_t instruction, bool full, bool defer_exit, bool delay) {
  const auto opcode = instruction >> 26;
  const auto function = instruction & 63;
  const auto target = (instruction >> 16) & 31;
  const bool immediate = opcode == 1 && target >= 8 && target <= 14 && target != 13;
  if (!immediate && !(opcode == 0 && function >= 48 && function <= 54 && function != 53))
    return false;
  constexpr sljit_s32 conditions[] = {
      SLJIT_SIG_GREATER_EQUAL, SLJIT_GREATER_EQUAL, SLJIT_SIG_LESS, SLJIT_LESS, SLJIT_EQUAL, 0,
      SLJIT_NOT_EQUAL};
  const auto condition = conditions[(immediate ? target : function) & 7];
  const auto source = gpr((instruction >> 21) & 31);
  const auto value = static_cast<std::uint64_t>(
      std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  const auto operand = immediate ? imm(value) : gpr(target);
  commit_pipeline();
  SlowPath path{{}, nullptr, instruction, cycles, delay ? 0u : 2u};
  path.enter.push_back(
      sljit_emit_cmp(compiler, condition, source.type, source.value, operand.type, operand.value));
  if (full) {
    begin(false);
    end(defer_exit, delay);
  } else {
    cycles += 2;
    pipeline_dirty = true;
  }
  path.resume = sljit_emit_label(compiler);
  slow_paths.push_back(std::move(path));
  return true;
}

} // namespace cupid::n64
