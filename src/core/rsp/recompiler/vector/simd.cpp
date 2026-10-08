#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>
#include <array>

namespace cupid::n64 {
namespace {

struct alignas(16) Mask {
  std::array<std::uint8_t, 16> bytes{};
};

struct Tables {
  std::array<Mask, 16> select{};
  std::array<std::array<std::array<std::array<Mask, 16>, 16>, 2>, 2> shuffle{}, preserve{};
  constexpr Tables() {
    for (unsigned element = 0; element < 16; ++element)
      for (unsigned lane = 0; lane < 8; ++lane) {
        const auto index = element < 2   ? lane
                           : element < 4 ? (lane & 6) | (element & 1)
                           : element < 8 ? (lane & 4) | (element & 3)
                                         : element & 7;
        select[element].bytes[lane * 2] = static_cast<std::uint8_t>(index * 2);
        select[element].bytes[lane * 2 + 1] = static_cast<std::uint8_t>(index * 2 + 1);
      }
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
constexpr Mask zero{};
constexpr Mask invert{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff}};
constexpr sljit_s32 simd = SLJIT_SIMD_REG_128 | SLJIT_SIMD_ELEM_8;

} // namespace

bool RspCompiler::Emitter::vector_multiply_simd(std::uint32_t instruction) {
#if SLJIT_CONFIG_X86_64
  const auto operation = instruction & 63;
  if ((operation != 4 && operation != 14 && operation != 15) ||
      !sljit_has_cpu_feature(SLJIT_HAS_SIMD))
    return false;
  const auto element = (instruction >> 21) & 15;
  if (element >= 2 && sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE | SLJIT_SIMD_TEST,
                                          SLJIT_VR0, SLJIT_VR0, SLJIT_VR1, 0) != SLJIT_SUCCESS)
    return false;
  std::array<sljit_s32, 5> registers;
  for (unsigned n = 0; n < registers.size(); ++n)
    registers[n] = sljit_get_register_index(SLJIT_SIMD_REG_128, SLJIT_VR0 + n);
  if (std::any_of(registers.begin(), registers.end(),
                  [](auto index) { return index < 0 || index > 15; }))
    return false;
  const auto vector = [&](unsigned index) -> Operand {
    return {SLJIT_MEM1(SLJIT_S0),
            static_cast<sljit_sw>(offsetof(RspState, vectors) + index * sizeof(RspVector))};
  };
  const auto accumulator = [&](std::size_t offset) -> Operand {
    return {SLJIT_MEM1(SLJIT_S0), static_cast<sljit_sw>(offsetof(RspState, accumulator) + offset)};
  };
  const auto low = accumulator(offsetof(RspAccumulator, low));
  const auto middle = accumulator(offsetof(RspAccumulator, middle));
  const auto high = accumulator(offsetof(RspAccumulator, high));
  const auto dest = vector((instruction >> 6) & 31);
  const auto load = [&](unsigned reg, Operand source) {
    sljit_emit_simd_mov(compiler, simd, SLJIT_VR0 + reg, source.type, source.value);
  };
  const auto copy = [&](unsigned dest, unsigned source) {
    sljit_emit_simd_mov(compiler, simd, SLJIT_VR0 + dest, SLJIT_VR0 + source, 0);
  };
  const auto store = [&](Operand dest, unsigned reg) {
    sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_STORE, SLJIT_VR0 + reg, dest.type, dest.value);
  };
  const auto logic = [&](sljit_s32 op, unsigned dest, Operand source) {
    sljit_emit_simd_op2(compiler, simd | op, SLJIT_VR0 + dest, SLJIT_VR0 + dest, source.type,
                        source.value);
  };
  const auto packed = [&](std::uint8_t opcode, unsigned dest, unsigned source) {
    std::array<std::uint8_t, 5> bytes{0x66};
    unsigned size = 1;
    const auto rex = 0x40 | ((registers[dest] >> 3) << 2) | (registers[source] >> 3);
    if (rex != 0x40)
      bytes[size++] = static_cast<std::uint8_t>(rex);
    bytes[size++] = 0x0f;
    bytes[size++] = opcode;
    bytes[size++] =
        static_cast<std::uint8_t>(0xc0 | ((registers[dest] & 7) << 3) | (registers[source] & 7));
    sljit_emit_op_custom(compiler, bytes.data(), size);
  };
  const auto sign = [&](unsigned reg) {
    std::array<std::uint8_t, 6> bytes{0x66};
    unsigned size = 1;
    if (registers[reg] >= 8)
      bytes[size++] = 0x41;
    bytes[size++] = 0x0f;
    bytes[size++] = 0x71;
    bytes[size++] = static_cast<std::uint8_t>(0xe0 | (registers[reg] & 7));
    bytes[size++] = 15;
    sljit_emit_op_custom(compiler, bytes.data(), size);
  };
  static constexpr Mask bias{
      {0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80}};
  const auto carry = [&](unsigned old, unsigned sum, unsigned temp) {
    logic(SLJIT_SIMD_OP2_XOR, old, field(&bias));
    copy(temp, sum);
    logic(SLJIT_SIMD_OP2_XOR, temp, field(&bias));
    packed(0x65, old, temp); // PCMPGTW after sign bias compares unsigned words.
  };
  load(0, vector((instruction >> 11) & 31));
  load(1, vector((instruction >> 16) & 31));
  if (element >= 2) {
    const auto mask = field(&tables.select[element]);
    sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE, SLJIT_VR1, SLJIT_VR1, mask.type,
                        mask.value);
  }
  if (operation == 4) {
    packed(0xe4, 0, 1); // PMULHUW: the upper half of each unsigned product.
    store(low, 0);
    store(dest, 0);
    load(0, field(&zero));
    store(middle, 0);
    store(high, 0);
    return true;
  }
  copy(3, 0);
  if (operation == 14) {
    copy(2, 0);
    packed(0xd5, 2, 1); // PMULLW
    packed(0xe4, 3, 1); // PMULHUW
    sign(1);
    logic(SLJIT_SIMD_OP2_AND, 1, reg(SLJIT_VR0));
    packed(0xf9, 3, 1); // PSUBW: correct the unsigned high product for the signed target.
    copy(1, 3);
    sign(1);
    load(4, low);
    packed(0xfd, 2, 4); // PADDW
    carry(4, 2, 0);
    store(low, 2);
  } else {
    packed(0xd5, 3, 1); // PMULLW
    packed(0xe5, 1, 0); // PMULHW
  }
  load(0, middle);
  packed(0xfd, 3, 0);
  carry(0, 3, 2);
  if (operation == 14) {
    load(2, field(&invert));
    packed(0x75, 2, 3); // PCMPEQW: adding a low carry wraps an all-ones middle word.
    logic(SLJIT_SIMD_OP2_AND, 2, reg(SLJIT_VR0 + 4));
    logic(SLJIT_SIMD_OP2_OR, 0, reg(SLJIT_VR0 + 2));
    packed(0xf9, 3, 4);
  }
  load(4, high);
  packed(0xfd, 1, 4);
  packed(0xf9, 1, 0);
  store(middle, 3);
  store(high, 1);
  if (operation == 14) {
    copy(4, 3);
    sign(4);
    packed(0x75, 4, 1);
    copy(0, 1);
    sign(0);
    logic(SLJIT_SIMD_OP2_XOR, 0, field(&invert));
    load(2, low);
    logic(SLJIT_SIMD_OP2_AND, 2, reg(SLJIT_VR0 + 4));
    packed(0xdf, 4, 0); // PANDN: select the unsigned saturation value outside the range.
    logic(SLJIT_SIMD_OP2_OR, 2, reg(SLJIT_VR0 + 4));
    store(dest, 2);
  } else {
    copy(0, 3);
    packed(0x61, 0, 1); // PUNPCKLWD
    packed(0x69, 3, 1); // PUNPCKHWD
    packed(0x6b, 0, 3); // PACKSSDW
    store(dest, 0);
  }
  return true;
#else
  (void)instruction;
  return false;
#endif
}

bool RspCompiler::Emitter::vector_arithmetic_simd(std::uint32_t instruction) {
  const auto operation = instruction & 63;
  if (operation != 29 && (operation < 40 || operation > 45))
    return false;
  if (!sljit_has_cpu_feature(SLJIT_HAS_SIMD))
    return false;
  const auto element = (instruction >> 21) & 15;
  const Operand dest{SLJIT_MEM1(SLJIT_S0),
                     static_cast<sljit_sw>(offsetof(RspState, vectors) +
                                           ((instruction >> 6) & 31) * sizeof(RspVector))};
  const auto accumulator = [&](std::size_t offset) -> Operand {
    return {SLJIT_MEM1(SLJIT_S0), static_cast<sljit_sw>(offsetof(RspState, accumulator) + offset)};
  };
  if (operation == 29) {
    const auto source = element == 8    ? accumulator(offsetof(RspAccumulator, high))
                        : element == 9  ? accumulator(offsetof(RspAccumulator, middle))
                        : element == 10 ? accumulator(offsetof(RspAccumulator, low))
                                        : field(&zero);
    sljit_emit_simd_mov(compiler, simd, SLJIT_VR0, source.type, source.value);
  } else {
    const auto logic = operation < 42   ? SLJIT_SIMD_OP2_AND
                       : operation < 44 ? SLJIT_SIMD_OP2_OR
                                        : SLJIT_SIMD_OP2_XOR;
    if (sljit_emit_simd_op2(compiler, simd | logic | SLJIT_SIMD_TEST, SLJIT_VR0, SLJIT_VR0,
                            SLJIT_VR1, 0) != SLJIT_SUCCESS ||
        (element >= 2 &&
         sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE | SLJIT_SIMD_TEST, SLJIT_VR0,
                             SLJIT_VR0, SLJIT_VR1, 0) != SLJIT_SUCCESS))
      return false;
    const Operand source{SLJIT_MEM1(SLJIT_S0),
                         static_cast<sljit_sw>(offsetof(RspState, vectors) +
                                               ((instruction >> 11) & 31) * sizeof(RspVector))};
    const Operand target{SLJIT_MEM1(SLJIT_S0),
                         static_cast<sljit_sw>(offsetof(RspState, vectors) +
                                               ((instruction >> 16) & 31) * sizeof(RspVector))};
    sljit_emit_simd_mov(compiler, simd, SLJIT_VR0, target.type, target.value);
    if (element >= 2) {
      const auto mask = field(&tables.select[element]);
      sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_SHUFFLE, SLJIT_VR0, SLJIT_VR0, mask.type,
                          mask.value);
    }
    sljit_emit_simd_op2(compiler, simd | logic, SLJIT_VR0, SLJIT_VR0, source.type, source.value);
    if (operation & 1) {
      const auto mask = field(&invert);
      sljit_emit_simd_op2(compiler, simd | SLJIT_SIMD_OP2_XOR, SLJIT_VR0, SLJIT_VR0, mask.type,
                          mask.value);
    }
    const auto low = accumulator(offsetof(RspAccumulator, low));
    sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_STORE, SLJIT_VR0, low.type, low.value);
  }
  sljit_emit_simd_mov(compiler, simd | SLJIT_SIMD_STORE, SLJIT_VR0, dest.type, dest.value);
  return true;
}

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
