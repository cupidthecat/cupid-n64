#include "emitter.hpp"

#if defined(CUPID_RSP_NATIVE)
namespace cupid::rsp_native {

void emit_clip(sljit_compiler* compiler, u32 word, bool destination_live, AccumulatorCache& cache) {
    VectorEmitter emit(compiler, cache);
    emit.flush();
    const bool cached = cache.enabled;
    cache.enabled = false;
    const unsigned function = word & 63U;
    const unsigned destination = (word >> 6U) & 31U;
    constexpr auto low = offsetof(RspNativeState, carry_low);
    constexpr auto high = offsetof(RspNativeState, carry_high);
    constexpr auto compare_low = offsetof(RspNativeState, compare_low);
    constexpr auto compare_high = offsetof(RspNativeState, compare_high);
    constexpr auto extension = offsetof(RspNativeState, compare_extension);
    emit.operands((word >> 11U) & 31U, (word >> 16U) & 31U, (word >> 21U) & 15U);
    emit.operation(Packed::Xor, 4, 4);

    unsigned result = 5;
    if (function == 36U) {
        emit.flag_masks(2, low);
        emit.flag_masks(5, high);
        // Form VT or its wrapped negation before updating the carry groups.
        emit.operation(Packed::Move, 6, 1);
        emit.operation(Packed::Xor, 6, 2);
        emit.operation(Packed::Subtract, 6, 2);
        emit.operation(Packed::Move, 7, 0);
        emit.operation(Packed::Add, 7, 1);
        emit.operation(Packed::Move, 3, 0);
        emit.operation(Packed::AddUnsigned, 3, 1);
        emit.operation(Packed::Equal, 3, 7);
        emit.operation(Packed::Equal, 7, 4);
        emit.operation(Packed::Move, 4, 7);
        emit.operation(Packed::And, 4, 3);
        emit.operation(Packed::Or, 7, 3);
        emit.flag_masks(1, extension);
        emit.operation(Packed::And, 7, 1);
        emit.operation(Packed::Or, 7, 4);
        emit.operation(Packed::Move, 1, 5);
        emit.operation(Packed::AndNot, 1, 2);
        emit.flag_masks(4, compare_low);
        emit.operation(Packed::And, 7, 1);
        emit.operation(Packed::AndNot, 1, 4);
        emit.operation(Packed::Or, 7, 1);
        emit.flag_copy(7, compare_low);

        emit.operation(Packed::Or, 5, 2);
        emit.operation(Packed::Xor, 4, 4);
        emit.operation(Packed::Equal, 5, 4);
        // Recover VT from the retained alternate operand for unsigned comparison.
        emit.operation(Packed::Move, 1, 6);
        emit.operation(Packed::Add, 1, 2);
        emit.operation(Packed::Xor, 1, 2);
        emit.operation(Packed::SubtractUnsigned, 1, 0);
        emit.operation(Packed::Equal, 1, 4);
        emit.flag_masks(4, compare_high);
        emit.operation(Packed::And, 1, 5);
        emit.operation(Packed::AndNot, 5, 4);
        emit.operation(Packed::Or, 1, 5);
        emit.flag_copy(1, compare_high);
        emit.operation(Packed::And, 7, 2);
        emit.operation(Packed::AndNot, 2, 1);
        emit.operation(Packed::Or, 7, 2);
        emit.operation(Packed::And, 6, 7);
        emit.operation(Packed::AndNot, 7, 0);
        emit.operation(Packed::Or, 6, 7);
        result = 6;
    } else {
        emit.operation(Packed::Move, 2, 0);
        emit.operation(Packed::Xor, 2, 1);
        emit.shift(2, 4, 15);
        emit.operation(Packed::Move, 5, 1);
        emit.operation(Packed::Xor, 5, 2);
        if (function == 37U) {
            emit.flag_copy(2, low);
            emit.operation(Packed::Subtract, 5, 2);
            emit.operation(Packed::Move, 6, 0);
            emit.operation(Packed::Subtract, 6, 5);
            emit.operation(Packed::Move, 7, 6);
            emit.operation(Packed::Equal, 7, 2);
            emit.operation(Packed::And, 7, 2);
            emit.flag_copy(7, extension);
            emit.operation(Packed::Move, 3, 6);
            emit.operation(Packed::Equal, 3, 4);
            emit.operation(Packed::Or, 7, 3);
            emit.operation(Packed::Equal, 7, 4);
            emit.flag_copy(7, high);

            emit.operation(Packed::Move, 7, 6);
            emit.operation(Packed::Greater, 7, 4);
            emit.operation(Packed::Equal, 7, 4);
            emit.operation(Packed::Move, 6, 1);
            emit.shift(6, 4, 15);
            emit.operation(Packed::And, 7, 2);
            emit.operation(Packed::Move, 3, 2);
            emit.operation(Packed::AndNot, 3, 6);
            emit.operation(Packed::Or, 7, 3);
            emit.flag_copy(7, compare_low);

            emit.operation(Packed::Move, 6, 0);
            emit.operation(Packed::Subtract, 6, 5);
            emit.shift(6, 4, 15);
            emit.operation(Packed::Equal, 6, 4);
            emit.shift(1, 4, 15);
            emit.operation(Packed::And, 1, 2);
            emit.operation(Packed::Move, 3, 2);
            emit.operation(Packed::AndNot, 3, 6);
            emit.operation(Packed::Or, 1, 3);
            emit.flag_copy(1, compare_high);
            emit.operation(Packed::And, 7, 2);
            emit.operation(Packed::AndNot, 2, 1);
            emit.operation(Packed::Or, 7, 2);
        } else {
            // Opposite signs use a one's complement rather than a negation.
            emit.operation(Packed::Move, 6, 0);
            emit.operation(Packed::And, 6, 2);
            emit.operation(Packed::Add, 6, 1);
            emit.shift(6, 4, 15);
            emit.operation(Packed::Move, 7, 0);
            emit.operation(Packed::Or, 7, 2);
            emit.operation(Packed::MinimumSigned, 7, 1);
            emit.operation(Packed::Equal, 7, 1);
            emit.flag_copy(6, compare_low);
            emit.flag_copy(7, compare_high);
            emit.operation(Packed::And, 6, 2);
            emit.operation(Packed::AndNot, 2, 7);
            emit.operation(Packed::Or, 6, 2);
            emit.operation(Packed::Move, 7, 6);
        }
        emit.operation(Packed::And, 5, 7);
        emit.operation(Packed::AndNot, 7, 0);
        emit.operation(Packed::Or, 5, 7);
    }
    emit.store(result, VectorEmitter::accumulator_base, 0);
    if (destination_live)
        emit.store(result, VectorEmitter::vector_base, destination * 16U);
    if (function != 37U) {
        emit.clear_flag(low);
        emit.clear_flag(high);
        emit.clear_flag(extension);
    }
    cache.enabled = cached;
}

} // namespace cupid::rsp_native
#endif