#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

bool RspCompiler::Emitter::branch(std::uint32_t instruction, std::uint32_t pc) {
  const auto opcode = instruction >> 26;
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto function = instruction & 63;
  if (opcode == 0) {
    if (function != 8 && function != 9)
      return false;
    op1(SLJIT_MOV32, reg(SLJIT_R0), gpr(source));
    if (function == 9)
      store((instruction >> 11) & 31, imm((pc + 8) & 0xfff));
    op2(SLJIT_AND32, field(&rsp.next_pc_), reg(SLJIT_R0), imm(0xfff));
  } else if (opcode == 2 || opcode == 3) {
    if (opcode == 3)
      store(31, imm((pc + 8) & 0xfff));
    op1(SLJIT_MOV32, field(&rsp.next_pc_), imm((instruction << 2) & 0xfff));
  } else if (opcode == 1 || (opcode >= 4 && opcode <= 7)) {
    op1(SLJIT_MOV32, reg(SLJIT_R0), gpr(source));
    auto right = imm(0);
    sljit_s32 taken;
    if (opcode == 1) {
      if (target != 0 && target != 1 && target != 16 && target != 17)
        return false;
      if (target & 16)
        store(31, imm((pc + 8) & 0xfff));
      taken = target & 1 ? SLJIT_SIG_GREATER_EQUAL : SLJIT_SIG_LESS;
    } else {
      if (opcode <= 5)
        right = gpr(target);
      taken = opcode == 4   ? SLJIT_EQUAL
              : opcode == 5 ? SLJIT_NOT_EQUAL
              : opcode == 6 ? SLJIT_SIG_LESS_EQUAL
                            : SLJIT_SIG_GREATER;
    }
    const auto skip =
        sljit_emit_cmp(compiler, (taken ^ 1) | SLJIT_32, SLJIT_R0, 0, right.type, right.value);
    const auto offset = static_cast<std::uint32_t>(static_cast<std::int16_t>(instruction));
    op1(SLJIT_MOV32, field(&rsp.next_pc_), imm((pc + 4 + offset * 4) & 0xfff));
    op1(SLJIT_MOV_U8, field(&rsp.next_delay_slot_), imm(1));
    sljit_set_label(skip, sljit_emit_label(compiler));
    return true;
  } else {
    return false;
  }
  op1(SLJIT_MOV_U8, field(&rsp.next_delay_slot_), imm(1));
  return true;
}

} // namespace cupid::n64
