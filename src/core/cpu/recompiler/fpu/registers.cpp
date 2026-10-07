#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

CpuCompiler::Emitter::Operand CpuCompiler::Emitter::fpr(unsigned index, bool word, bool high) {
  auto result = state(offsetof(CpuState, fpr) + index * 8);
  if (word && high == (std::endian::native == std::endian::little))
    result.value += 4;
  return result;
}

void CpuCompiler::Emitter::floating_transfer(std::uint32_t instruction) {
  const auto format = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto source = (instruction >> 11) & 31;
  if (format == 16 || format == 17) {
    op1(SLJIT_MOV, fpr((instruction >> 6) & 31), fpr(cpu.fpu_source(source)));
    return;
  }
  const bool word = !(format & 1);
  const bool high = !(cpu.control_[Status] & 0x04000000) && (source & 1);
  const auto value = fpr(cpu.fpu_source(source), word, high);
  if (format < 2) {
    op1(word ? SLJIT_MOV_S32 : SLJIT_MOV, reg(SLJIT_R0), value);
    store(target, reg(SLJIT_R0));
  } else {
    op1(word ? SLJIT_MOV32 : SLJIT_MOV, value, gpr(target));
  }
}

} // namespace cupid::n64
