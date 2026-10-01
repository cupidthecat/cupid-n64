#include "vector.hpp"

#include "cupid/rsp/native.hpp"
#include <cstddef>

#if defined(CUPID_RSP_NATIVE)
#include <array>

namespace cupid::rsp_native {
namespace {

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

} // namespace

void flush_accumulator(sljit_compiler* compiler, AccumulatorCache& cache) {
    VectorEmitter(compiler, cache).flush();
}

bool supports_vector(unsigned function) {
    return function <= 1U || (function >= 4U && function <= 9U) || (function >= 12U && function <= 15U) ||
           function == 16U || function == 17U || function == 19U || function == 20U || function == 21U ||
           function == 29U || (function >= 32U && function <= 35U) || function == 39U || function == 50U ||
           function == 51U || function == 54U || (function >= 0x28U && function <= 0x2dU);
}

void emit_vector(sljit_compiler* compiler, u32 word, bool& comparison_bias_live, bool destination_live,
                 AccumulatorCache& cache) {
    VectorEmitter emit(compiler, cache);
    const unsigned function = word & 63U;
    const unsigned element = (word >> 21U) & 15U;
    const unsigned destination = (word >> 6U) & 31U;
    constexpr auto vectors = VectorEmitter::vector_base;
    constexpr auto accumulator = VectorEmitter::accumulator_base;
    const auto bias = [&] {
        if (!comparison_bias_live) {
            emit.operation(Packed::Equal, 4, 4);
            emit.shift(4, 6, 15);
            comparison_bias_live = true;
        }
    };
    const auto signed_middle = [&] {
        if (!destination_live)
            return;
        emit.operation(Packed::Move, 0, 2);
        emit.operation(Packed::Move, 1, 2);
        emit.operation(Packed::UnpackLow, 0, 3);
        emit.operation(Packed::UnpackHigh, 1, 3);
        emit.operation(Packed::PackSigned, 0, 1);
    };
    const auto unsigned_middle = [&] {
        if (!destination_live)
            return;
        emit.operation(Packed::Move, 1, 3);
        emit.shift(1, 4, 15);
        emit.operation(Packed::Move, 0, 2);
        emit.shift(0, 4, 15);
        emit.operation(Packed::Or, 2, 0);
        emit.operation(Packed::Xor, 0, 0);
        emit.operation(Packed::Greater, 3, 0);
        emit.operation(Packed::AndNot, 1, 2);
        emit.operation(Packed::Or, 1, 3);
        emit.operation(Packed::Move, 0, 1);
    };
    const auto unsigned_low = [&] {
        if (!destination_live)
            return;
        emit.operation(Packed::Move, 1, 2);
        emit.shift(1, 4, 15);
        emit.operation(Packed::Equal, 1, 3);
        emit.operation(Packed::Move, 2, 3);
        emit.shift(2, 4, 15);
        emit.operation(Packed::Xor, 3, 3);
        emit.operation(Packed::Equal, 2, 3);
        emit.operation(Packed::And, 0, 1);
        emit.operation(Packed::AndNot, 1, 2);
        emit.operation(Packed::Or, 0, 1);
    };
    // Product slices occupy 0, 2, and 3. Register 1 holds carry masks.
    const auto add_accumulator = [&] {
        bias();
        if (cache.enabled) {
            const unsigned low = emit.cached_slice(0), middle = emit.cached_slice(16),
                           high = emit.cached_slice(32);
            emit.operation(Packed::Move, 1, low);
            emit.operation(Packed::Add, low, 0);
            emit.operation(Packed::Move, 0, low);
            emit.operation(Packed::Xor, 1, 4);
            emit.operation(Packed::Xor, 0, 4);
            emit.operation(Packed::Greater, 1, 0);
            emit.operation(Packed::Subtract, 2, 1);
            emit.operation(Packed::Xor, 0, 0);
            emit.operation(Packed::Equal, 0, 2);
            emit.operation(Packed::And, 0, 1);
            emit.operation(Packed::Subtract, 3, 0);
            emit.operation(Packed::Move, 1, middle);
            emit.operation(Packed::Add, middle, 2);
            emit.operation(Packed::Xor, 1, 4);
            emit.operation(Packed::Move, 0, middle);
            emit.operation(Packed::Xor, 0, 4);
            emit.operation(Packed::Greater, 1, 0);
            emit.operation(Packed::Add, high, 3);
            emit.operation(Packed::Subtract, high, 1);
            emit.modified_slice(0);
            emit.modified_slice(16);
            emit.modified_slice(32);
            if (destination_live) {
                emit.operation(Packed::Move, 2, middle);
                emit.operation(Packed::Move, 3, high);
                if (function == 12U || function == 14U)
                    emit.operation(Packed::Move, 0, low);
            }
            return;
        }
        emit.load(1, accumulator, 0);
        emit.operation(Packed::Add, 0, 1);
        emit.store(0, accumulator, 0);
        emit.operation(Packed::Xor, 1, 4);
        emit.operation(Packed::Xor, 0, 4);
        emit.operation(Packed::Greater, 1, 0);
        emit.operation(Packed::Subtract, 2, 1);
        // A low carry can wrap the product's middle slice before its addition.
        emit.operation(Packed::Xor, 0, 0);
        emit.operation(Packed::Equal, 0, 2);
        emit.operation(Packed::And, 0, 1);
        emit.operation(Packed::Subtract, 3, 0);
        emit.load(1, accumulator, 16);
        emit.operation(Packed::Add, 2, 1);
        emit.store(2, accumulator, 16);
        emit.operation(Packed::Xor, 1, 4);
        emit.operation(Packed::Move, 0, 2);
        emit.operation(Packed::Xor, 0, 4);
        emit.operation(Packed::Greater, 1, 0);
        emit.load(0, accumulator, 32);
        emit.operation(Packed::Add, 3, 0);
        emit.operation(Packed::Subtract, 3, 1);
        emit.store(3, accumulator, 32);
        if (destination_live && (function == 12U || function == 14U))
            emit.load(0, accumulator, 0);
    };

    if (function == 29U) {
        if (!destination_live)
            return;
        if (element >= 8U && element <= 10U)
            emit.load(0, accumulator, (10U - element) * 16U);
        else
            emit.operation(Packed::Xor, 0, 0);
        emit.store(0, vectors, destination * 16U);
        return;
    }
    const bool partial = function == 50U || function == 51U || function == 54U;
    emit.operands((word >> 11U) & 31U, (word >> 16U) & 31U, element, !partial);
    if (partial) {
        const unsigned lane = (word >> 11U) & 7U;
        emit.store(1, accumulator, 0);
        if (function == 51U) {
            if (destination_live) {
                sljit_emit_simd_lane_mov(
                    compiler, SLJIT_SIMD_REG_128 | SLJIT_SIMD_ELEM_16 | SLJIT_SIMD_STORE | SLJIT_32,
                    SLJIT_VR1, static_cast<sljit_s32>(lane), SLJIT_R0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_MEM1(vectors), destination * 16U + lane * 2U,
                               SLJIT_R0, 0);
            }
        } else {
            // Read the raw input element before an aliased destination lane changes it.
            sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_R0, 0, SLJIT_MEM1(vectors),
                           ((word >> 16U) & 31U) * 16U + (element & 7U) * 2U);
            sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                           offsetof(RspNativeState, divider_input));
            sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_MEM1(SLJIT_R3), 0, SLJIT_R0, 0);
            sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                           offsetof(RspNativeState, divider_high));
            sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_R3), 0, SLJIT_IMM, 1);
            if (destination_live) {
                sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_S5),
                               offsetof(RspNativeState, divider_output));
                sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3), 0);
                sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_MEM1(vectors), destination * 16U + lane * 2U,
                               SLJIT_R0, 0);
            }
        }
        return;
    }
    if (function >= 0x28U && function <= 0x2dU) {
        emit.operation(function < 0x2aU ? Packed::And : function < 0x2cU ? Packed::Or : Packed::Xor, 0, 1);
        if ((function & 1U) != 0U) {
            emit.operation(Packed::Equal, 1, 1);
            emit.operation(Packed::Xor, 0, 1);
        }
        emit.store(0, accumulator, 0);
        if (destination_live)
            emit.store(0, vectors, destination * 16U);
        return;
    }
    if (function == 19U) {
        emit.operation(Packed::Move, 3, 0);
        emit.shift(3, 4, 15);
        emit.operation(Packed::Xor, 2, 2);
        emit.operation(Packed::Greater, 0, 2);
        emit.operation(Packed::Move, 2, 1);
        emit.operation(Packed::And, 2, 0);
        emit.operation(Packed::Xor, 0, 0);
        emit.operation(Packed::Subtract, 0, 1);
        emit.operation(Packed::And, 0, 3);
        emit.operation(Packed::Or, 0, 2);
        emit.store(0, accumulator, 0);
        if (destination_live) {
            emit.operation(Packed::Xor, 0, 0);
            emit.operation(Packed::SubtractSigned, 0, 1);
            emit.operation(Packed::And, 0, 3);
            emit.operation(Packed::Or, 0, 2);
            emit.store(0, vectors, destination * 16U);
        }
        return;
    }
    if ((function >= 32U && function <= 35U) || function == 39U) {
        if (function == 39U) {
            emit.flag_masks(2, offsetof(RspNativeState, compare_low));
        } else {
            emit.flag_masks(2, offsetof(RspNativeState, carry_high),
                            function == 32U || function == 35U ? offsetof(RspNativeState, carry_low) : 0);
            emit.operation(Packed::Move, 3, 0);
            emit.operation(Packed::Equal, 3, 1);
            if (function == 32U) {
                emit.operation(Packed::And, 2, 3);
                emit.operation(Packed::Move, 3, 1);
                emit.operation(Packed::Greater, 3, 0);
                emit.operation(Packed::Or, 2, 3);
            } else {
                emit.operation(Packed::AndNot, 2, 3);
                if (function == 34U) {
                    emit.operation(Packed::Xor, 3, 3);
                    emit.operation(Packed::Equal, 2, 3);
                } else if (function == 35U) {
                    emit.operation(Packed::Move, 3, 0);
                    emit.operation(Packed::Greater, 3, 1);
                    emit.operation(Packed::Or, 2, 3);
                }
            }
            emit.operation(Packed::Move, 3, 2);
            emit.flag(3, offsetof(RspNativeState, compare_low));
            emit.clear_flag(offsetof(RspNativeState, compare_high));
        }
        emit.operation(Packed::And, 0, 2);
        emit.operation(Packed::AndNot, 2, 1);
        emit.operation(Packed::Or, 0, 2);
        emit.store(0, accumulator, 0);
        if (destination_live)
            emit.store(0, vectors, destination * 16U);
        emit.clear_carry(false);
        emit.clear_carry(true);
        return;
    }
    if (function == 16U || function == 17U) {
        emit.carry_values(2);
        if (function == 16U) {
            emit.operation(Packed::Move, 3, 0);
            emit.operation(Packed::Add, 3, 1);
            emit.operation(Packed::Add, 3, 2);
            emit.store(3, accumulator, 0);
            if (destination_live) {
                emit.operation(Packed::Move, 3, 0);
                // Adding carry to the smaller operand preserves both saturation endpoints.
                emit.operation(Packed::MinimumSigned, 3, 1);
                emit.operation(Packed::MaximumSigned, 0, 1);
                emit.operation(Packed::AddSigned, 3, 2);
                emit.operation(Packed::AddSigned, 3, 0);
                emit.store(3, vectors, destination * 16U);
            }
        } else {
            emit.operation(Packed::Move, 3, 1);
            emit.operation(Packed::Add, 1, 2);
            emit.operation(Packed::AddSigned, 3, 2);
            emit.operation(Packed::Move, 2, 0);
            emit.operation(Packed::Subtract, 2, 1);
            emit.store(2, accumulator, 0);
            if (destination_live) {
                emit.operation(Packed::Move, 2, 3);
                // Recover the extra unit when VT + carry wrapped past signed positive range.
                emit.operation(Packed::Greater, 2, 1);
                emit.operation(Packed::SubtractSigned, 0, 3);
                emit.operation(Packed::AddSigned, 0, 2);
                emit.store(0, vectors, destination * 16U);
            }
        }
        emit.clear_carry(false);
        emit.clear_carry(true);
        return;
    }
    if (function == 20U || function == 21U) {
        emit.operation(Packed::Move, 2, function == 20U ? 0U : 1U);
        emit.operation(Packed::Move, 3, 0);
        emit.operation(function == 20U ? Packed::Add : Packed::Subtract, 0, 1);
        emit.store(0, accumulator, 0);
        if (destination_live)
            emit.store(0, vectors, destination * 16U);
        bias();
        if (function == 20U)
            emit.operation(Packed::Move, 3, 0);
        emit.operation(Packed::Xor, 2, 4);
        emit.operation(Packed::Xor, 3, 4);
        emit.operation(Packed::Greater, 2, 3);
        emit.carry_flag(2, false);
        if (function == 20U) {
            emit.clear_carry(true);
        } else {
            emit.operation(Packed::Move, 3, 0);
            emit.operation(Packed::Xor, 1, 1);
            emit.operation(Packed::Equal, 3, 1);
            emit.operation(Packed::Equal, 1, 1);
            emit.operation(Packed::Xor, 3, 1);
            emit.carry_flag(3, true);
        }
        return;
    }
    if (function <= 1U) {
        // Fractional multiply rounds the doubled signed product by 0x8000.
        // Equal negative operands identify the positive 0x80000000 endpoint.
        emit.operation(Packed::Move, 3, 0);
        emit.operation(Packed::Equal, 3, 1);
        emit.operation(Packed::Move, 2, 0);
        emit.operation(Packed::MultiplyHighSigned, 2, 1);
        emit.operation(Packed::MultiplyLow, 0, 1);
        emit.operation(Packed::Move, 1, 0);
        emit.shift(1, 2, 15);
        emit.shift(0, 6, 1);
        emit.operation(Packed::Move, 4, 0);
        emit.shift(4, 2, 15);
        emit.operation(Packed::Add, 1, 4);
        comparison_bias_live = false;
        bias();
        emit.operation(Packed::Add, 0, 4);
        emit.store(0, accumulator, 0);
        emit.shift(2, 6, 1);
        emit.operation(Packed::Add, 2, 1);
        emit.store(2, accumulator, 16);
        emit.operation(Packed::Move, 1, 2);
        emit.shift(1, 4, 15);
        if (destination_live && function == 0U) {
            emit.operation(Packed::Move, 0, 3);
            emit.operation(Packed::And, 0, 1);
        }
        emit.operation(Packed::AndNot, 3, 1);
        emit.store(3, accumulator, 32);
        if (!destination_live)
            return;
        if (function == 0U) {
            emit.operation(Packed::Add, 2, 0);
            emit.store(2, vectors, destination * 16U);
        } else {
            emit.operation(Packed::Or, 2, 1);
            emit.operation(Packed::AndNot, 3, 2);
            emit.store(3, vectors, destination * 16U);
        }
        return;
    }
    if (function == 4U || function == 12U) {
        emit.operation(Packed::MultiplyHighUnsigned, 0, 1);
        emit.operation(Packed::Xor, 2, 2);
        emit.operation(Packed::Xor, 3, 3);
        if (function == 4U) {
            emit.store(0, accumulator, 0);
            emit.store(2, accumulator, 16);
            emit.store(3, accumulator, 32);
        } else {
            add_accumulator();
            unsigned_low();
        }
        if (destination_live)
            emit.store(0, vectors, destination * 16U);
        return;
    }
    emit.operation(Packed::Move, 2, 0);
    if (function == 5U || function == 6U || function == 13U || function == 14U) {
        const bool signed_left = function == 5U || function == 13U;
        emit.operation(Packed::MultiplyHighUnsigned, 2, 1);
        emit.operation(Packed::Move, 3, signed_left ? 0U : 1U);
        emit.shift(3, 4, 15);
        emit.operation(Packed::And, 3, signed_left ? 1U : 0U);
        emit.operation(Packed::Subtract, 2, 3);
        emit.operation(Packed::MultiplyLow, 0, 1);
        emit.operation(Packed::Move, 3, 2);
        emit.shift(3, 4, 15);
        if (function < 8U) {
            emit.store(0, accumulator, 0);
            emit.store(2, accumulator, 16);
            emit.store(3, accumulator, 32);
            if (destination_live)
                emit.store(function == 5U ? 2U : 0U, vectors, destination * 16U);
            return;
        }
        add_accumulator();
        if (function == 13U)
            signed_middle();
        else
            unsigned_low();
        if (destination_live)
            emit.store(0, vectors, destination * 16U);
        return;
    }
    emit.operation(Packed::MultiplyHighSigned, 2, 1);
    emit.operation(Packed::MultiplyLow, 0, 1);
    if (function == 8U || function == 9U) {
        emit.operation(Packed::Move, 3, 2);
        emit.shift(3, 4, 15);
        emit.operation(Packed::Move, 1, 0);
        emit.shift(1, 2, 15);
        emit.shift(0, 6, 1);
        emit.shift(2, 6, 1);
        emit.operation(Packed::Or, 2, 1);
        add_accumulator();
        if (function == 8U)
            signed_middle();
        else
            unsigned_middle();
        if (destination_live)
            emit.store(0, vectors, destination * 16U);
        return;
    }
    if (function == 15U && cache.enabled) {
        const unsigned middle = emit.cached_slice(16), high = emit.cached_slice(32);
        emit.operation(Packed::Move, 1, middle);
        emit.operation(Packed::Add, 0, middle);
        emit.operation(Packed::Add, high, 2);
        bias();
        emit.operation(Packed::Xor, 1, 4);
        emit.operation(Packed::Move, 3, 0);
        emit.operation(Packed::Xor, 3, 4);
        emit.operation(Packed::Greater, 1, 3);
        emit.operation(Packed::Subtract, high, 1);
        emit.store(0, accumulator, 16);
        emit.modified_slice(32);
        if (destination_live) {
            emit.operation(Packed::Move, 1, 0);
            emit.operation(Packed::UnpackLow, 0, high);
            emit.operation(Packed::UnpackHigh, 1, high);
            emit.operation(Packed::PackSigned, 0, 1);
            emit.store(0, vectors, destination * 16U);
        }
        return;
    }
    if (function == 7U) {
        emit.operation(Packed::Xor, 1, 1);
        emit.store(1, accumulator, 0);
    } else {
        emit.load(1, accumulator, 16);
        emit.operation(Packed::Add, 0, 1);
        emit.load(3, accumulator, 32);
        emit.operation(Packed::Add, 2, 3);
        bias();
        emit.operation(Packed::Xor, 1, 4);
        emit.operation(Packed::Move, 3, 0);
        emit.operation(Packed::Xor, 3, 4);
        emit.operation(Packed::Greater, 1, 3);
        emit.operation(Packed::Subtract, 2, 1);
    }
    emit.store(0, accumulator, 16);
    emit.store(2, accumulator, 32);
    if (!destination_live)
        return;
    emit.operation(Packed::Move, 1, 0);
    emit.operation(Packed::UnpackLow, 0, 2);
    emit.operation(Packed::UnpackHigh, 1, 2);
    emit.operation(Packed::PackSigned, 0, 1);
    emit.store(0, vectors, destination * 16U);
}

} // namespace cupid::rsp_native
#endif
