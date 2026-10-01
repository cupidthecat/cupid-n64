#pragma once

#include "cupid/rsp/native.hpp"
#include "vector.hpp"

#if defined(CUPID_RSP_NATIVE)
#include <array>
#include <cstddef>

namespace cupid::rsp_native {
enum class Packed : u8 {
    And = 0xdb,
    AndNot = 0xdf,
    Or = 0xeb,
    Xor = 0xef,
    Move = 0x6f,
    Equal = 0x75,
    Greater = 0x65,
    MultiplyLow = 0xd5,
    MultiplyHighSigned = 0xe5,
    MultiplyHighUnsigned = 0xe4,
    Add = 0xfd,
    Subtract = 0xf9,
    UnpackLow = 0x61,
    UnpackHigh = 0x69,
    PackSigned = 0x6b,
    PackBytes = 0x63,
    AddUnsigned = 0xdd,
    SubtractUnsigned = 0xd9,
    AddSigned = 0xed,
    SubtractSigned = 0xe9,
    MinimumSigned = 0xea,
    MaximumSigned = 0xee,
};

class VectorEmitter {
  public:
    VectorEmitter(sljit_compiler* compiler, AccumulatorCache& cache) : compiler_(compiler), cache_(cache) {}

    void operation(Packed opcode, unsigned output, unsigned input) {
        const auto dst = physical(output), src = physical(input);
        std::array<u8, 5> bytes{0x66U};
        unsigned size = 1;
        if (dst >= 8U || src >= 8U)
            bytes[size++] = static_cast<u8>(0x40U | ((dst >> 3U) << 2U) | (src >> 3U));
        bytes[size++] = 0x0fU;
        bytes[size++] = static_cast<u8>(opcode);
        bytes[size++] = static_cast<u8>(0xc0U | ((dst & 7U) << 3U) | (src & 7U));
        sljit_emit_op_custom(compiler_, bytes.data(), static_cast<sljit_u32>(size));
    }

    void shift(unsigned index, unsigned group, u8 count) {
        const auto reg = physical(index);
        std::array<u8, 6> bytes{0x66U};
        unsigned size = 1;
        if (reg >= 8U)
            bytes[size++] = 0x41U;
        bytes[size++] = 0x0fU;
        bytes[size++] = 0x71U;
        bytes[size++] = static_cast<u8>(0xc0U | (group << 3U) | (reg & 7U));
        bytes[size++] = count;
        sljit_emit_op_custom(compiler_, bytes.data(), static_cast<sljit_u32>(size));
    }

    void load(unsigned index, sljit_s32 base, unsigned offset) {
        if (cache_.enabled && base == accumulator_base) {
            operation(Packed::Move, index, cached_slice(offset));
            return;
        }
        sljit_emit_simd_mov(compiler_, type, SLJIT_VR(static_cast<sljit_s32>(index)), SLJIT_MEM1(base),
                            offset);
    }

    void store(unsigned index, sljit_s32 base, unsigned offset) {
        if (cache_.enabled && base == accumulator_base) {
            operation(Packed::Move, 5U + offset / 16U, index);
            modified_slice(offset);
            return;
        }
        sljit_emit_simd_mov(compiler_, type | SLJIT_SIMD_STORE, SLJIT_VR(static_cast<sljit_s32>(index)),
                            SLJIT_MEM1(base), offset);
    }

    unsigned cached_slice(unsigned offset) {
        const unsigned slice = offset / 16U;
        const auto bit = static_cast<u8>(1U << slice);
        const unsigned index = 5U + slice;
        if ((cache_.valid & bit) == 0) {
            sljit_emit_simd_mov(compiler_, type, SLJIT_VR(static_cast<sljit_s32>(index)),
                                SLJIT_MEM1(accumulator_base), offset);
            cache_.valid |= bit;
        }
        return index;
    }

    void modified_slice(unsigned offset) {
        const auto bit = static_cast<u8>(1U << (offset / 16U));
        cache_.valid |= bit;
        cache_.dirty |= bit;
    }

    void flag_masks(unsigned index, std::size_t offset, std::size_t second = 0) {
        static constexpr std::array<u16, 8> bits{1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U};
        sljit_emit_op1(compiler_, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                       static_cast<sljit_sw>(offset));
        sljit_emit_op1(compiler_, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3), 0);
        if (second != 0) {
            sljit_emit_op1(compiler_, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                           static_cast<sljit_sw>(second));
            sljit_emit_op1(compiler_, SLJIT_MOV_U8, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R3), 0);
            sljit_emit_op2(compiler_, SLJIT_AND | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        }
        sljit_emit_simd_replicate(compiler_, type, SLJIT_VR(static_cast<sljit_s32>(index)), SLJIT_R0, 0);
        sljit_emit_op1(compiler_, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_IMM,
                       reinterpret_cast<sljit_sw>(bits.data()));
        load(3, SLJIT_R3, 0);
        operation(Packed::And, index, 3);
        operation(Packed::Xor, 3, 3);
        operation(Packed::Greater, index, 3);
    }

    void carry_values(unsigned index) {
        flag_masks(index, offsetof(RspNativeState, carry_low));
        shift(index, 2, 15);
    }

    void flag(unsigned index, std::size_t offset) {
        // Each signed mask becomes one byte; its lower eight sign bits encode lanes.
        operation(Packed::PackBytes, index, index);
        sljit_emit_simd_sign(compiler_, SLJIT_SIMD_REG_128 | SLJIT_SIMD_ELEM_8 | SLJIT_SIMD_STORE | SLJIT_32,
                             SLJIT_VR(static_cast<sljit_s32>(index)), SLJIT_R0, 0);
        sljit_emit_op1(compiler_, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                       static_cast<sljit_sw>(offset));
        sljit_emit_op1(compiler_, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_R3), 0, SLJIT_R0, 0);
    }

    void flag_copy(unsigned index, std::size_t offset) {
        operation(Packed::Move, 3, index);
        flag(3, offset);
    }

    void carry_flag(unsigned index, bool high) {
        flag(index, high ? offsetof(RspNativeState, carry_high) : offsetof(RspNativeState, carry_low));
    }

    void clear_flag(std::size_t offset) {
        sljit_emit_op1(compiler_, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                       static_cast<sljit_sw>(offset));
        sljit_emit_op1(compiler_, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_R3), 0, SLJIT_IMM, 0);
    }

    void clear_carry(bool high) {
        clear_flag(high ? offsetof(RspNativeState, carry_high) : offsetof(RspNativeState, carry_low));
    }

    void flush() {
        for (unsigned slice = 0; slice < 3U; ++slice)
            if ((cache_.dirty & (1U << slice)) != 0)
                sljit_emit_simd_mov(compiler_, type | SLJIT_SIMD_STORE,
                                    SLJIT_VR(static_cast<sljit_s32>(5U + slice)),
                                    SLJIT_MEM1(accumulator_base), slice * 16U);
        cache_.valid = cache_.dirty = 0;
    }

    void operands(unsigned source, unsigned target, unsigned element, bool read_source = true) {
        // Snapshot both inputs before storing an aliased destination.
        if (read_source)
            load(0, vector_base, source * 16U);
        if (element >= 8U) {
            sljit_emit_simd_replicate(compiler_, type, SLJIT_VR1, SLJIT_MEM1(vector_base),
                                      target * 16U + (element - 8U) * 2U);
            return;
        }
        load(1, vector_base, target * 16U);
        if (element < 2U)
            return;
        constexpr std::array<u8, 8> selections{0, 0, 0xa0, 0xf5, 0, 0x55, 0xaa, 0xff};
        const auto reg = physical(1);
        for (u8 prefix : {u8{0xf2}, u8{0xf3}}) {
            std::array<u8, 6> bytes{prefix};
            unsigned size = 1;
            if (reg >= 8U)
                bytes[size++] = 0x45U;
            bytes[size++] = 0x0fU;
            bytes[size++] = 0x70U;
            bytes[size++] = static_cast<u8>(0xc0U | ((reg & 7U) << 3U) | (reg & 7U));
            bytes[size++] = selections[element];
            sljit_emit_op_custom(compiler_, bytes.data(), static_cast<sljit_u32>(size));
        }
    }

    static constexpr sljit_s32 vector_base = SLJIT_S3;
    static constexpr sljit_s32 accumulator_base = SLJIT_S4;

  private:
    static constexpr auto type = SLJIT_SIMD_REG_128 | SLJIT_SIMD_ELEM_16;
    sljit_compiler* compiler_;
    AccumulatorCache& cache_;

    static unsigned physical(unsigned index) {
        return static_cast<unsigned>(
            sljit_get_register_index(SLJIT_SIMD_REG_128, SLJIT_VR(static_cast<sljit_s32>(index))));
    }
};

} // namespace cupid::rsp_native
#endif
