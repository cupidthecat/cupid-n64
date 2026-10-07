#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

void RspCompiler::Emitter::vector_linear(std::uint32_t instruction, std::uint32_t pc) {
  const bool store = (instruction >> 26) == 58;
  const auto operation = (instruction >> 11) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto element = (instruction >> 7) & 15;
  const auto length = store ? 1u << operation : std::min(1u << operation, 16 - element);
  const bool pairs = !(element & 1) && length >= 2;
  std::vector<sljit_jump *> slow;
  if (pairs) {
    slow.push_back(
        sljit_emit_cmp(compiler, SLJIT_GREATER | SLJIT_32, SLJIT_R1, 0, SLJIT_IMM, 4096 - length));
#if !SLJIT_UNALIGNED
    op2(SLJIT_AND32, reg(SLJIT_R0), reg(SLJIT_R1), imm(1));
    slow.push_back(sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0));
#endif
  }
  for (unsigned n = 0; n < length;) {
    const bool pair = pairs && n + 1 < length;
    vector_address(n);
    const Operand memory{SLJIT_MEM2(SLJIT_S2, SLJIT_R0)};
    const auto value = pair ? vector_half(target, ((element + n) & 15) >> 1)
                            : vector_byte(target, (element + n) & 15);
    const auto move = pair ? SLJIT_MOV_U16 : SLJIT_MOV_U8;
    op1(move, reg(SLJIT_R3), store ? value : memory);
#if SLJIT_LITTLE_ENDIAN
    if (pair)
      op1(SLJIT_REV32_U16, reg(SLJIT_R3), reg(SLJIT_R3));
#endif
    op1(move, store ? memory : value, reg(SLJIT_R3));
    n += pair ? 2 : 1;
  }
  if (!slow.empty()) {
    const auto resume = sljit_emit_label(compiler);
    for (const auto jump : slow)
      memory_paths.push_back({jump, resume, instruction, pc});
  }
}

} // namespace cupid::n64
