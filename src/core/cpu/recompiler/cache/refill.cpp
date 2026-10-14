#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

sljit_jump *CpuCompiler::Emitter::refill(std::uint32_t address) {
  const auto *tracker = cpu.bus_.instruction_tracker();
  if (diagnostics || !tracker || !tracker->fully_tracked())
    return nullptr;
  const auto index = static_cast<unsigned>(pc) & 0xfe0;
  auto &line = cpu.icache_[(pc >> 5) & 511];
  op1(SLJIT_MOV, reg(SLJIT_R1), field(&cpu.native_fill_page_));
  const auto unavailable = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
  const auto words = field(&cpu.native_fill_words_);
  const auto truncated =
      sljit_emit_cmp(compiler, SLJIT_LESS, words.type, words.value, SLJIT_IMM, index / 4 + 8);
  advance(96);
  op2(SLJIT_ADD, field(&cpu.instruction_cache_generation_),
      field(&cpu.instruction_cache_generation_), imm(1));
  op1(SLJIT_MOV_U8, field(&cpu.native_fetch_miss_), imm(1));
  op1(SLJIT_MOV_U8, field(&line.dirty), imm(0));
  op1(SLJIT_MOV32, field(&line.tag_key), imm((address & ~0xfffu) | 1));
  constexpr auto simd = SLJIT_SIMD_REG_128 | SLJIT_SIMD_ELEM_8;
  if (sljit_has_cpu_feature(SLJIT_HAS_SIMD) &&
      sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_TEST, SLJIT_VR0, SLJIT_MEM1(SLJIT_R1),
                          index) == SLJIT_SUCCESS) {
    for (unsigned offset : {0u, 16u}) {
      sljit_emit_simd_mov(compiler, simd, SLJIT_VR0, SLJIT_MEM1(SLJIT_R1), index + offset);
      const auto target = field(line.words.data() + offset / 4);
      sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_STORE, SLJIT_VR0, target.type, target.value);
    }
  } else {
    for (unsigned word = 0; word < 8; ++word) {
      op1(SLJIT_MOV_U32, reg(SLJIT_R0), {SLJIT_MEM1(SLJIT_R1), index + word * 4});
      op1(SLJIT_MOV32, field(&line.words[word]), reg(SLJIT_R0));
    }
  }
  const auto ready = sljit_emit_jump(compiler, SLJIT_JUMP);
  const auto fallback = sljit_emit_label(compiler);
  sljit_set_label(unavailable, fallback);
  sljit_set_label(truncated, fallback);
  return ready;
}

} // namespace cupid::n64
