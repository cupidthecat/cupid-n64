#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

void CpuCompiler::Emitter::advance(unsigned clocks) {
  if (!clocks)
    return;
  op2(SLJIT_ADD, reg(SLJIT_R0), state(offsetof(CpuState, clocks)), imm(clocks));
  op1(SLJIT_MOV, state(offsetof(CpuState, clocks)), reg(SLJIT_R0));
}

} // namespace cupid::n64
