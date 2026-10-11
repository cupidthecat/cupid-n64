#include "core/cpu/recompiler/compiler.hpp"
#if SLJIT_CONFIG_ARM_64
#include "core/cpu/fpu/host/arm64.hpp"
#endif

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
#elif SLJIT_CONFIG_ARM_64
void instruction(sljit_compiler *compiler, std::uint32_t opcode) {
  sljit_emit_op_custom(compiler, &opcode, sizeof(opcode));
}

void control(sljit_compiler *compiler, bool load, bool status, sljit_s32 reg) {
  const auto index = sljit_get_register_index(SLJIT_GP_REGISTER, reg);
  instruction(compiler, (load ? 0xd51b4400u : 0xd53b4400u) | (status ? 32 : 0) | index);
}
#endif

} // namespace

bool CpuCompiler::Emitter::floating_host() {
#if SLJIT_CONFIG_X86_64 || SLJIT_CONFIG_ARM_64
  return true;
#else
  return false;
#endif
}

void CpuCompiler::Emitter::floating_environment(unsigned rounding, bool flush) {
#if SLJIT_CONFIG_X86_64
  constexpr unsigned modes[] = {0, 0x6000, 0x4000, 0x2000};
  host_address(SLJIT_R2, block.floating_control.data());
  control(compiler, false, 0);
  op1(SLJIT_MOV32, {SLJIT_MEM1(SLJIT_R2), 4},
      imm(0x1f80 | (flush ? 0x8000 : 0) | modes[rounding & 3]));
  control(compiler, true, 4);
#elif SLJIT_CONFIG_ARM_64
  host_address(SLJIT_R2, block.floating_control.data());
  control(compiler, false, false, SLJIT_R3);
  op1(SLJIT_MOV, {SLJIT_MEM1(SLJIT_R2), 0}, reg(SLJIT_R3));
  control(compiler, false, true, SLJIT_R3);
  op1(SLJIT_MOV, {SLJIT_MEM1(SLJIT_R2), 8}, reg(SLJIT_R3));
  op1(SLJIT_MOV, reg(SLJIT_R3),
      imm((flush ? 0x01000000 : 0) | floating::arm64::rounding_modes[rounding & 3]));
  control(compiler, true, false, SLJIT_R3);
  op1(SLJIT_MOV, reg(SLJIT_R3), imm(0));
  control(compiler, true, true, SLJIT_R3);
#else
  (void)rounding;
  (void)flush;
#endif
}

void CpuCompiler::Emitter::floating_restore() {
#if SLJIT_CONFIG_X86_64
  host_address(SLJIT_R2, block.floating_control.data());
  control(compiler, false, 4);
  control(compiler, true, 0);
  op1(SLJIT_MOV_U32, reg(SLJIT_R3), {SLJIT_MEM1(SLJIT_R2), 4});
#elif SLJIT_CONFIG_ARM_64
  control(compiler, false, true, SLJIT_R3);
  host_address(SLJIT_R2, block.floating_control.data());
  op1(SLJIT_MOV, reg(SLJIT_R0), {SLJIT_MEM1(SLJIT_R2), 0});
  control(compiler, true, false, SLJIT_R0);
  op1(SLJIT_MOV, reg(SLJIT_R0), {SLJIT_MEM1(SLJIT_R2), 8});
  control(compiler, true, true, SLJIT_R0);
  // Normalize host flags to the layout consumed by the shared guest flag checks.
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R3), imm(0x80));
  op2(SLJIT_LSHR | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), imm(6));
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R2), reg(SLJIT_R3), imm(1));
  op2(SLJIT_OR | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), reg(SLJIT_R2));
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R2), reg(SLJIT_R3), imm(0x1e));
  op2(SLJIT_SHL | SLJIT_32, reg(SLJIT_R2), reg(SLJIT_R2), imm(1));
  op2(SLJIT_OR | SLJIT_32, reg(SLJIT_R3), reg(SLJIT_R0), reg(SLJIT_R2));
  // Flushed ARM64 underflows raise UFC without IXC; guest underflow requires both.
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R3), imm(16));
  op2(SLJIT_SHL | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), imm(1));
  op2(SLJIT_OR | SLJIT_32, reg(SLJIT_R3), reg(SLJIT_R3), reg(SLJIT_R0));
#endif
}

void CpuCompiler::Emitter::floating_sqrt(bool dual) {
#if SLJIT_CONFIG_X86_64
  scalar(compiler, 0x51, dual, false, false);
#elif SLJIT_CONFIG_ARM_64
  const auto source = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR0);
  instruction(compiler, 0x1e21c000u | (dual ? 1u << 22 : 0) | (source << 5) | source);
#else
  (void)dual;
#endif
}

void CpuCompiler::Emitter::floating_to_integer(bool dual, bool integer_dual, unsigned rounding) {
#if SLJIT_CONFIG_X86_64
  scalar(compiler, 0x2d, dual, true, integer_dual);
  (void)rounding;
#elif SLJIT_CONFIG_ARM_64
  constexpr std::uint32_t modes[]{0x1e200000, 0x1e380000, 0x1e280000, 0x1e300000};
  const auto source = sljit_get_register_index(SLJIT_FLOAT_REGISTER, SLJIT_FR0);
  const auto dest = sljit_get_register_index(SLJIT_GP_REGISTER, SLJIT_R1);
  instruction(compiler, modes[rounding & 3] | (dual ? 1u << 22 : 0) |
                            (integer_dual ? 1u << 31 : 0) | (source << 5) | dest);
#else
  (void)dual;
  (void)integer_dual;
  (void)rounding;
#endif
}

} // namespace cupid::n64
