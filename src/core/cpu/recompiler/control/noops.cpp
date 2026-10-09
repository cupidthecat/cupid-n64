#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

bool CpuCompiler::Emitter::control_noop(std::uint32_t instruction, bool full, bool defer_exit,
                                        bool delay) {
  if (!native_control_noop(instruction))
    return false;
  if (full)
    begin(false);
  if (full)
    end(defer_exit, delay);
  else {
    cycles += 2;
    pipeline_dirty = true;
  }
  return true;
}

} // namespace cupid::n64
