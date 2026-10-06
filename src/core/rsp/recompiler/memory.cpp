#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

bool RspCompiler::Emitter::memory(std::uint32_t instruction, std::uint32_t pc) {
  const auto opcode = instruction >> 26;
  unsigned bytes;
  switch (opcode) {
  case 32:
  case 36:
  case 40:
    bytes = 1;
    break;
  case 33:
  case 37:
  case 41:
    bytes = 2;
    break;
  case 35:
  case 39:
  case 43:
    bytes = 4;
    break;
  default:
    return false;
  }
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const bool write = opcode >= 40;
  if (!write && !target)
    return true;
  const auto offset = static_cast<std::uint32_t>(
      std::int32_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  op2(SLJIT_ADD32, reg(SLJIT_R1), gpr(source), imm(offset));
  op2(SLJIT_AND32, reg(SLJIT_R1), reg(SLJIT_R1), imm(0xfff));
  sljit_jump *slow = nullptr;
  if (bytes > 1) {
#if SLJIT_UNALIGNED
    slow = sljit_emit_cmp(compiler, SLJIT_GREATER | SLJIT_32, SLJIT_R1, 0, SLJIT_IMM, 4096 - bytes);
#else
    op2(SLJIT_AND32, reg(SLJIT_R0), reg(SLJIT_R1), imm(bytes - 1));
    slow = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
#endif
  }
  op1(SLJIT_MOV, reg(SLJIT_R2), imm(reinterpret_cast<std::uintptr_t>(rsp.memory_.data())));
  const Operand address{SLJIT_MEM2(SLJIT_R2, SLJIT_R1)};
  if (write) {
    op1(SLJIT_MOV32, reg(SLJIT_R0), gpr(target));
#if SLJIT_LITTLE_ENDIAN
    if (bytes > 1)
      op1(bytes == 2 ? SLJIT_REV32_U16 : SLJIT_REV32, reg(SLJIT_R0), reg(SLJIT_R0));
#endif
    op1(bytes == 1   ? SLJIT_MOV_U8
        : bytes == 2 ? SLJIT_MOV_U16
                     : SLJIT_MOV32,
        address, reg(SLJIT_R0));
  } else {
    op1(bytes == 1   ? (opcode == 32 ? SLJIT_MOV32_S8 : SLJIT_MOV32_U8)
        : bytes == 2 ? SLJIT_MOV32_U16
                     : SLJIT_MOV32,
        reg(SLJIT_R0), address);
#if SLJIT_LITTLE_ENDIAN
    if (bytes > 1)
      op1(bytes == 4     ? SLJIT_REV32
          : opcode == 33 ? SLJIT_REV32_S16
                         : SLJIT_REV32_U16,
          reg(SLJIT_R0), reg(SLJIT_R0));
#else
    if (opcode == 33)
      op1(SLJIT_MOV32_S16, reg(SLJIT_R0), reg(SLJIT_R0));
#endif
    store(target, reg(SLJIT_R0));
  }
  if (slow)
    memory_paths.push_back({slow, sljit_emit_label(compiler), instruction, pc});
  return true;
}

} // namespace cupid::n64
