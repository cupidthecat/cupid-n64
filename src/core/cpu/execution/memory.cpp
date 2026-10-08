#include "core/cpu/execution/memory.hpp"
#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

bool Cpu::native_cache_hit(std::uint32_t instruction, std::uint64_t ram_bytes) const {
  const auto memory = memory_instruction(instruction);
  if (!memory.bytes || (memory.wide && mode() != Mode::Kernel && !extended_addressing()) ||
      (memory.floating && !(control_[Status] & 0x20000000)))
    return false;
  const auto offset =
      std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction)));
  const auto address = state_.gpr[(instruction >> 21) & 31] + static_cast<std::uint64_t>(offset);
  const auto physical = address - 0xffffffff80000000ull;
  if (physical >= ram_bytes || (!memory.partial && (address & (memory.bytes - 1))))
    return false;
  return dcache_[(address >> 4) & 511].hit(static_cast<std::uint32_t>(physical));
}

} // namespace cupid::n64
