#include "vector.hpp"

#if defined(CUPID_RSP_NATIVE)
#include <array>

namespace cupid::rsp_native {
namespace {

enum class Packed : u8 {
    And = 0xdb,
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
};

class VectorEmitter {
  public:
    explicit VectorEmitter(sljit_compiler* compiler) : compiler_(compiler) {}

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
        sljit_emit_simd_mov(compiler_, type, SLJIT_VR(static_cast<sljit_s32>(index)), SLJIT_MEM1(base),
                            offset);
    }

    void store(unsigned index, sljit_s32 base, unsigned offset) {
        sljit_emit_simd_mov(compiler_, type | SLJIT_SIMD_STORE, SLJIT_VR(static_cast<sljit_s32>(index)),
                            SLJIT_MEM1(base), offset);
    }

    void operands(unsigned source, unsigned target, unsigned element) {
        // Snapshot both inputs before storing an aliased destination.
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

    static unsigned physical(unsigned index) {
        return static_cast<unsigned>(
            sljit_get_register_index(SLJIT_SIMD_REG_128, SLJIT_VR(static_cast<sljit_s32>(index))));
    }
};

} // namespace

bool supports_vector(unsigned function) {
    return (function >= 4U && function <= 7U) || function == 15U || function == 29U ||
           (function >= 0x28U && function <= 0x2dU);
}

void emit_vector(sljit_compiler* compiler, u32 word, bool& comparison_bias_live) {
    VectorEmitter emit(compiler);
    const unsigned function = word & 63U;
    const unsigned element = (word >> 21U) & 15U;
    const unsigned destination = (word >> 6U) & 31U;
    constexpr auto vectors = VectorEmitter::vector_base;
    constexpr auto accumulator = VectorEmitter::accumulator_base;

    if (function == 29U) {
        if (element >= 8U && element <= 10U)
            emit.load(0, accumulator, (10U - element) * 16U);
        else
            emit.operation(Packed::Xor, 0, 0);
        emit.store(0, vectors, destination * 16U);
        return;
    }

    emit.operands((word >> 11U) & 31U, (word >> 16U) & 31U, element);
    if (function >= 0x28U && function <= 0x2dU) {
        emit.operation(function < 0x2aU ? Packed::And : function < 0x2cU ? Packed::Or : Packed::Xor, 0, 1);
        if ((function & 1U) != 0U) {
            emit.operation(Packed::Equal, 1, 1);
            emit.operation(Packed::Xor, 0, 1);
        }
        emit.store(0, accumulator, 0);
        emit.store(0, vectors, destination * 16U);
        return;
    }

    if (function == 4U) {
        emit.operation(Packed::MultiplyHighUnsigned, 0, 1);
        emit.operation(Packed::Xor, 1, 1);
        emit.store(0, accumulator, 0);
        emit.store(1, accumulator, 16);
        emit.store(1, accumulator, 32);
        emit.store(0, vectors, destination * 16U);
        return;
    }

    emit.operation(Packed::Move, 2, 0);
    if (function == 5U || function == 6U) {
        emit.operation(Packed::MultiplyHighUnsigned, 2, 1);
        emit.operation(Packed::Move, 3, function == 5U ? 0U : 1U);
        emit.shift(3, 4, 15); // PSRAW forms the signed operand's mask.
        emit.operation(Packed::And, 3, function == 5U ? 1U : 0U);
        emit.operation(Packed::Subtract, 2, 3);
        emit.operation(Packed::MultiplyLow, 0, 1);
        emit.operation(Packed::Move, 3, 2);
        emit.shift(3, 4, 15);
        emit.store(0, accumulator, 0);
        emit.store(2, accumulator, 16);
        emit.store(3, accumulator, 32);
        emit.store(function == 5U ? 2U : 0U, vectors, destination * 16U);
        return;
    }

    emit.operation(Packed::MultiplyHighSigned, 2, 1);
    emit.operation(Packed::MultiplyLow, 0, 1);
    if (function == 7U) {
        emit.operation(Packed::Xor, 1, 1);
        emit.store(1, accumulator, 0);
    } else {
        // VMADH adds the signed product to bits 16..47. The low slice
        // survives, and an unsigned middle-slice carry reaches the high slice.
        emit.load(1, accumulator, 16);
        emit.operation(Packed::Add, 0, 1);
        emit.load(3, accumulator, 32);
        emit.operation(Packed::Add, 2, 3);
        if (!comparison_bias_live) {
            emit.operation(Packed::Equal, 4, 4);
            emit.shift(4, 6, 15); // PSLLW produces the unsigned comparison bias.
            comparison_bias_live = true;
        }
        emit.operation(Packed::Xor, 1, 4);
        emit.operation(Packed::Move, 3, 0);
        emit.operation(Packed::Xor, 3, 4);
        emit.operation(Packed::Greater, 1, 3);
        emit.operation(Packed::Subtract, 2, 1);
    }
    emit.store(0, accumulator, 16);
    emit.store(2, accumulator, 32);
    emit.operation(Packed::Move, 1, 0);
    emit.operation(Packed::UnpackLow, 0, 2);
    emit.operation(Packed::UnpackHigh, 1, 2);
    emit.operation(Packed::PackSigned, 0, 1);
    emit.store(0, vectors, destination * 16U);
}

} // namespace cupid::rsp_native
#endif
