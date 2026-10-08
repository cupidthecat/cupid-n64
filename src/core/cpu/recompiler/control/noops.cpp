#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

bool CpuCompiler::Emitter::control_noop(std::uint32_t instruction, bool full, bool defer_exit) {
  if ((instruction >> 26) != 16)
    return false;
  const auto format = (instruction >> 21) & 31;
  if (format != 2 && format != 6 && format != 8)
    return false;
  if (full)
    begin(false);
  if (full)
    end(defer_exit);
  else {
    cycles += 2;
    pipeline_dirty = true;
  }
  return true;
}

} // namespace cupid::n64
