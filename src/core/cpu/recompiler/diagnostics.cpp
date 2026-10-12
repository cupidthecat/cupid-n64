#include "core/cpu/execution/memory.hpp"
#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::profile_fetch(Cpu *cpu) {
  if (!cpu->native_fetch_miss_ && cpu->diagnostics_)
    cpu->diagnostics_->cache_access(true, true);
  cpu->native_fetch_miss_ = false;
}

void CpuCompiler::Emitter::profile_memory(Cpu *cpu, std::uint32_t instruction, sljit_uw physical) {
  const auto memory = memory_instruction(instruction);
  if (!cpu->diagnostics_ || (memory.linked && memory.write && !cpu->llbit_))
    return;
  cpu->diagnostics_->cache_access(false, true);
  if (memory.write) {
    const auto offset = static_cast<std::uint64_t>(
        std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
    const auto address = cpu->state_.gpr[(instruction >> 21) & 31] + offset;
    cpu->diagnostics_->cache_dirty(
        static_cast<unsigned>((address >> 4) & 511), static_cast<std::uint32_t>(physical),
        static_cast<std::uint16_t>(((1u << memory.bytes) - 1) << (physical & 15)), cpu->state_.pc);
  }
}

} // namespace cupid::n64
