#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {
namespace {

#if SLJIT_CONFIG_X86_64
void control(sljit_compiler *compiler, bool load, unsigned offset) {
  const auto base = sljit_get_register_index(SLJIT_GP_REGISTER, SLJIT_R2);
  std::array<std::uint8_t, 5> code{
      static_cast<std::uint8_t>(0x40 | (base >> 3)), 0x0f, 0xae,
      static_cast<std::uint8_t>(0x40 | ((load ? 2 : 3) << 3) | (base & 7)),
      static_cast<std::uint8_t>(offset)};
  sljit_emit_op_custom(compiler, code.data(), static_cast<unsigned>(code.size()));
}

void scalar(sljit_compiler *compiler, unsigned opcode, bool dual, bool integer, bool wide) {
  const auto source = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR0);
  const auto dest = integer ? sljit_get_register_index(SLJIT_GP_REGISTER, SLJIT_R1) : source;
  std::array<std::uint8_t, 5> code{
      static_cast<std::uint8_t>(dual ? 0xf2 : 0xf3),
      static_cast<std::uint8_t>(0x40 | (wide ? 8 : 0) | ((dest >> 3) << 2) | (source >> 3)), 0x0f,
      static_cast<std::uint8_t>(opcode),
      static_cast<std::uint8_t>(0xc0 | ((dest & 7) << 3) | (source & 7))};
  sljit_emit_op_custom(compiler, code.data(), static_cast<unsigned>(code.size()));
}
#endif

} // namespace

bool CpuCompiler::Emitter::floating_host() {
#if SLJIT_CONFIG_X86_64
  return true;
#else
  return false;
#endif
}

void CpuCompiler::Emitter::floating_environment(unsigned rounding) {
#if SLJIT_CONFIG_X86_64
  constexpr unsigned modes[] = {0, 0x6000, 0x4000, 0x2000};
  op1(SLJIT_MOV, reg(SLJIT_R2),
      imm(reinterpret_cast<std::uintptr_t>(block.floating_control.data())));
  control(compiler, false, 0);
  op1(SLJIT_MOV32, {SLJIT_MEM1(SLJIT_R2), 4}, imm(0x9f80 | modes[rounding & 3]));
  control(compiler, true, 4);
#else
  (void)rounding;
#endif
}

void CpuCompiler::Emitter::floating_restore() {
#if SLJIT_CONFIG_X86_64
  op1(SLJIT_MOV, reg(SLJIT_R2),
      imm(reinterpret_cast<std::uintptr_t>(block.floating_control.data())));
  control(compiler, false, 4);
  control(compiler, true, 0);
  op1(SLJIT_MOV_U32, reg(SLJIT_R3), {SLJIT_MEM1(SLJIT_R2), 4});
#endif
}

void CpuCompiler::Emitter::floating_sqrt(bool dual) {
#if SLJIT_CONFIG_X86_64
  scalar(compiler, 0x51, dual, false, false);
#else
  (void)dual;
#endif
}

void CpuCompiler::Emitter::floating_to_integer(bool dual, bool integer_dual) {
#if SLJIT_CONFIG_X86_64
  scalar(compiler, 0x2d, dual, true, integer_dual);
#else
  (void)dual;
  (void)integer_dual;
#endif
}

} // namespace cupid::n64
