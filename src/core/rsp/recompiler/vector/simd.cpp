#include "core/rsp/recompiler/compiler.hpp"
#include <array>

namespace cupid::n64 {
namespace {

struct alignas(16) Mask {
  std::array<std::uint8_t, 16> bytes{};
};

struct Tables {
  std::array<std::array<std::array<std::array<Mask, 16>, 16>, 2>, 2> shuffle{}, preserve{};
  constexpr Tables() {
    for (unsigned store = 0; store < 2; ++store)
      for (unsigned reverse = 0; reverse < 2; ++reverse)
        for (unsigned element = 0; element < 16; ++element)
          for (unsigned address = 0; address < 16; ++address)
            for (unsigned byte = 0; byte < 16; ++byte) {
              unsigned index = 0x80;
#if SLJIT_LITTLE_ENDIAN
              const auto logical = byte ^ 1;
#else
              const auto logical = byte;
#endif
              if (!store) {
                if (!reverse && logical >= element && logical - element < 16 - address)
                  index = address + logical - element;
                if (reverse && address > element && logical >= 16 - (address - element))
                  index = logical - (16 - (address - element));
              } else {
                if (!reverse && byte >= address)
                  index = (element + byte - address) & 15;
                if (reverse && byte < address)
                  index = (element + 16 - address + byte) & 15;
#if SLJIT_LITTLE_ENDIAN
                if (index != 0x80)
                  index ^= 1;
#endif
              }
              shuffle[store][reverse][element][address].bytes[byte] =
                  static_cast<std::uint8_t>(index);
              preserve[store][reverse][element][address].bytes[byte] = index == 0x80 ? 0xff : 0;
            }
  }
};
constexpr Tables tables;
constexpr sljit_s32 simd = SLJIT_SIMD_REG_128 | SLJIT_SIMD_ELEM_8;

} // namespace

bool RspCompiler::Emitter::vector_quad_simd(std::uint32_t instruction) {
  if (!sljit_has_cpu_feature(SLJIT_HAS_SIMD) ||
      sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE | SLJIT_SIMD_TEST, SLJIT_VR0,
                          SLJIT_VR0, SLJIT_VR1, 0) != SLJIT_SUCCESS)
    return false;
  const bool store = (instruction >> 26) == 58;
  const auto reverse = ((instruction >> 11) & 31) == 5;
  const auto target = (instruction >> 16) & 31;
  const auto element = (instruction >> 7) & 15;
  op2(SLJIT_AND32, reg(SLJIT_R0), reg(SLJIT_R1), imm(15));
  const Operand vector{SLJIT_MEM1(SLJIT_S0), static_cast<sljit_sw>(offsetof(RspState, vectors) +
                                                                   target * sizeof(RspVector))};
  const Operand memory{SLJIT_MEM2(SLJIT_S2, SLJIT_R1)};
  const auto source = store ? vector : memory;
  const auto dest = store ? memory : vector;
  sljit_jump *done = nullptr;
  if (reverse)
    done = sljit_emit_cmp(compiler, (store ? SLJIT_EQUAL : SLJIT_LESS_EQUAL) | SLJIT_32, SLJIT_R0,
                          0, SLJIT_IMM, store ? 0 : element);
  else if (!element) {
    const auto partial =
        sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
    sljit_emit_simd_mov(compiler, simd, SLJIT_VR0, source.type, source.value);
    const auto mask = field(&tables.shuffle[store][0][0][0]);
    sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE, SLJIT_VR0, SLJIT_VR0, mask.type,
                        mask.value);
    sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_STORE, SLJIT_VR0, dest.type, dest.value);
    done = sljit_emit_jump(compiler, SLJIT_JUMP);
    sljit_set_label(partial, sljit_emit_label(compiler));
  }
  op2(SLJIT_SHL32, reg(SLJIT_R0), reg(SLJIT_R0), imm(4));
  op2(SLJIT_AND32, reg(SLJIT_R1), reg(SLJIT_R1), imm(0xff0));
  sljit_emit_simd_mov(compiler, simd, SLJIT_VR0, source.type, source.value);
  op1(SLJIT_MOV, reg(SLJIT_R3),
      imm(reinterpret_cast<std::uintptr_t>(&tables.shuffle[store][reverse][element])));
  sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE, SLJIT_VR0, SLJIT_VR0,
                      SLJIT_MEM2(SLJIT_R3, SLJIT_R0), 0);
  sljit_emit_simd_mov(compiler, simd, SLJIT_VR1, dest.type, dest.value);
  op1(SLJIT_MOV, reg(SLJIT_R3),
      imm(reinterpret_cast<std::uintptr_t>(&tables.preserve[store][reverse][element])));
  sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_AND, SLJIT_VR1, SLJIT_VR1,
                      SLJIT_MEM2(SLJIT_R3, SLJIT_R0), 0);
  sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_OR, SLJIT_VR0, SLJIT_VR0, SLJIT_VR1, 0);
  sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_STORE, SLJIT_VR0, dest.type, dest.value);
  if (done)
    sljit_set_label(done, sljit_emit_label(compiler));
  return true;
}

} // namespace cupid::n64
