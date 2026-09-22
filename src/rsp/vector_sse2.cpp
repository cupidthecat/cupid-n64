#include "cupid/rsp.hpp"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define CUPID_RSP_HAS_SSE2 1
#include "vector_lanes.hpp"
#else
#define CUPID_RSP_HAS_SSE2 0
#endif

namespace cupid {

bool Rsp::execute_vector_op_sse2(u32 instruction) {
#if !CUPID_RSP_HAS_SSE2
    (void)instruction;
    return false;
#else
    using namespace rsp_vector;
    using PackedAccumulator = rsp_vector::Accumulator;

    struct Operands {
        __m128i left;
        __m128i right;
    };

    const unsigned function = instruction & 63U;
    const unsigned element = (instruction >> 21U) & 15U;
    auto& destination = vr_[(instruction >> 6U) & 31U].lane;
    const __m128i zero = _mm_setzero_si128();
    const auto snapshot_operands = [&]() -> Operands {
        // Read both complete operands before committing a destination that may alias
        // either source, including a shuffled VT that is also VS and VD.
        return {load_vector(vr_[(instruction >> 11U) & 31U].lane),
                select_vector(vr_[(instruction >> 16U) & 31U].lane, element)};
    };
    const auto read_slices = [&]() -> PackedAccumulator {
        return {load_bytes(accumulator_.low.data()), load_bytes(accumulator_.middle.data()),
                load_bytes(accumulator_.high.data())};
    };
    const auto write_slices = [&](PackedAccumulator value) {
        store_bytes(accumulator_.low.data(), value.low);
        store_bytes(accumulator_.middle.data(), value.middle);
        store_bytes(accumulator_.high.data(), value.high);
    };
    const auto flag_mask = [&](u8 flags) { return _mm_cmpgt_epi16(flag_values(flags), zero); };
    const auto select = [](__m128i mask, __m128i left, __m128i right) {
        return _mm_or_si128(_mm_and_si128(mask, left), _mm_andnot_si128(mask, right));
    };

    const auto multiply = [&]<unsigned Function>() -> bool {
        static_assert(Function == 0x00U || Function == 0x01U || Function == 0x04U || Function == 0x05U ||
                      Function == 0x06U || Function == 0x07U || Function == 0x08U || Function == 0x09U ||
                      Function == 0x0cU || Function == 0x0dU || Function == 0x0eU || Function == 0x0fU);
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;

        PackedAccumulator product{};
        if constexpr (Function == 0x04U || Function == 0x0cU) {
            product = {_mm_mulhi_epu16(left, right), zero, zero};
        } else if constexpr (Function == 0x05U || Function == 0x0dU) {
            const __m128i high =
                _mm_sub_epi16(_mm_mulhi_epu16(left, right), _mm_and_si128(right, _mm_srai_epi16(left, 15)));
            product = {_mm_mullo_epi16(left, right), high, _mm_srai_epi16(high, 15)};
        } else if constexpr (Function == 0x06U || Function == 0x0eU) {
            const __m128i high =
                _mm_sub_epi16(_mm_mulhi_epu16(left, right), _mm_and_si128(left, _mm_srai_epi16(right, 15)));
            product = {_mm_mullo_epi16(left, right), high, _mm_srai_epi16(high, 15)};
        } else {
            const __m128i high = _mm_mulhi_epi16(left, right);
            product = {_mm_mullo_epi16(left, right), high, _mm_srai_epi16(high, 15)};
        }

        if constexpr (Function == 0x07U || Function == 0x0fU) {
            product = {zero, product.low, product.middle};
        } else if constexpr (Function == 0x00U || Function == 0x01U || Function == 0x08U ||
                             Function == 0x09U) {
            // Shift the signed product across all three slices before rounding or
            // accumulation. The positive 0x40000000 product needs its 33rd result bit.
            product = {_mm_slli_epi16(product.low, 1),
                       _mm_or_si128(_mm_slli_epi16(product.middle, 1), _mm_srli_epi16(product.low, 15)),
                       _mm_or_si128(_mm_slli_epi16(product.high, 1), _mm_srli_epi16(product.middle, 15))};
            if constexpr (Function == 0x00U || Function == 0x01U)
                product = add(product, {_mm_set1_epi16(-32768), zero, zero});
        }

        PackedAccumulator result = product;
        if constexpr (Function >= 0x08U)
            result = add(read_slices(), product);
        write_slices(result);

        if constexpr (Function == 0x01U || Function == 0x09U)
            store_vector(destination, unsigned_middle(result));
        else if constexpr (Function == 0x04U || Function == 0x06U)
            store_vector(destination, result.low);
        else if constexpr (Function == 0x05U)
            store_vector(destination, result.middle);
        else if constexpr (Function == 0x0cU || Function == 0x0eU)
            store_vector(destination, unsigned_low(result));
        else
            store_vector(destination, signed_middle(result));
        return true;
    };

    switch (function) {
    case 0x00:
        return multiply.template operator()<0x00U>();
    case 0x01:
        return multiply.template operator()<0x01U>();
    case 0x04:
        return multiply.template operator()<0x04U>();
    case 0x05:
        return multiply.template operator()<0x05U>();
    case 0x06:
        return multiply.template operator()<0x06U>();
    case 0x07:
        return multiply.template operator()<0x07U>();
    case 0x08:
        return multiply.template operator()<0x08U>();
    case 0x09:
        return multiply.template operator()<0x09U>();
    case 0x0c:
        return multiply.template operator()<0x0cU>();
    case 0x0d:
        return multiply.template operator()<0x0dU>();
    case 0x0e:
        return multiply.template operator()<0x0eU>();
    case 0x0f:
        return multiply.template operator()<0x0fU>();
    case 0x10:
    case 0x11: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i carry = flag_values(vcol_);
        const Wide32 left_wide = widen_signed(left);
        const Wide32 right_wide = widen_signed(right);
        const Wide32 carry_wide = widen_unsigned(carry);
        Wide32 result{};
        __m128i low{};
        if (function == 0x10) {
            result.low = _mm_add_epi32(_mm_add_epi32(left_wide.low, right_wide.low), carry_wide.low);
            result.high = _mm_add_epi32(_mm_add_epi32(left_wide.high, right_wide.high), carry_wide.high);
            low = _mm_add_epi16(_mm_add_epi16(left, right), carry);
        } else {
            result.low = _mm_sub_epi32(_mm_sub_epi32(left_wide.low, right_wide.low), carry_wide.low);
            result.high = _mm_sub_epi32(_mm_sub_epi32(left_wide.high, right_wide.high), carry_wide.high);
            low = _mm_sub_epi16(_mm_sub_epi16(left, right), carry);
        }
        store_bytes(accumulator_.low.data(), low);
        store_vector(destination, _mm_packs_epi32(result.low, result.high));
        vcol_ = vcoh_ = 0;
        return true;
    }
    case 0x13: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i negative = _mm_srai_epi16(left, 15);
        const __m128i controlled = _mm_andnot_si128(_mm_cmpeq_epi16(left, zero), right);
        const __m128i flipped = _mm_xor_si128(controlled, negative);
        const __m128i accumulator = _mm_sub_epi16(flipped, negative);
        store_bytes(accumulator_.low.data(), accumulator);
        store_vector(destination, _mm_subs_epi16(flipped, negative));
        return true;
    }
    case 0x14: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i result = _mm_add_epi16(left, right);
        vcol_ = flags_from_mask(unsigned_less(result, left));
        vcoh_ = 0;
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x15: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i result = _mm_sub_epi16(left, right);
        vcol_ = flags_from_mask(unsigned_less(left, right));
        vcoh_ = flags_from_mask(_mm_cmpeq_epi16(_mm_cmpeq_epi16(left, right), zero));
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x1d: {
        __m128i result = zero;
        if (element == 8U)
            result = load_bytes(accumulator_.high.data());
        else if (element == 9U)
            result = load_bytes(accumulator_.middle.data());
        else if (element == 10U)
            result = load_bytes(accumulator_.low.data());
        store_vector(destination, result);
        return true;
    }
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i equal = _mm_cmpeq_epi16(left, right);
        const __m128i old_vcoh = flag_mask(vcoh_);
        __m128i mask{};
        if (function == 0x20) {
            const __m128i less = _mm_cmpgt_epi16(right, left);
            mask = _mm_or_si128(less, _mm_and_si128(equal, _mm_and_si128(flag_mask(vcol_), old_vcoh)));
        } else if (function == 0x21) {
            mask = _mm_andnot_si128(old_vcoh, equal);
        } else if (function == 0x22) {
            const __m128i not_equal = _mm_cmpeq_epi16(equal, zero);
            mask = _mm_or_si128(not_equal, old_vcoh);
        } else {
            const __m128i greater = _mm_cmpgt_epi16(left, right);
            const __m128i both = _mm_and_si128(flag_mask(vcol_), old_vcoh);
            mask = _mm_or_si128(greater, _mm_andnot_si128(both, equal));
        }
        const __m128i result = select(mask, left, right);
        vccl_ = flags_from_mask(mask);
        vcch_ = 0;
        vcol_ = vcoh_ = 0;
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x24: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i old_vcol = flag_mask(vcol_);
        const __m128i old_vcoh = flag_mask(vcoh_);
        const __m128i old_vccl = flag_mask(vccl_);
        const __m128i old_vcch = flag_mask(vcch_);
        const __m128i old_vce = flag_mask(vce_);

        const __m128i negated_right = _mm_sub_epi16(_mm_xor_si128(right, old_vcol), old_vcol);
        const __m128i sum = _mm_add_epi16(left, right);
        const __m128i carry = unsigned_less(sum, left);
        const __m128i no_carry = _mm_cmpeq_epi16(carry, zero);
        const __m128i sum_zero = _mm_cmpeq_epi16(sum, zero);
        const __m128i low_without_vce = _mm_and_si128(sum_zero, no_carry);
        const __m128i low_with_vce = _mm_or_si128(sum_zero, no_carry);
        const __m128i recomputed_low = select(old_vce, low_with_vce, low_without_vce);
        const __m128i update_low = _mm_andnot_si128(old_vcoh, old_vcol);
        const __m128i new_vccl = select(update_low, recomputed_low, old_vccl);

        const __m128i greater_equal = _mm_cmpeq_epi16(unsigned_less(left, right), zero);
        const __m128i update_high = _mm_cmpeq_epi16(_mm_or_si128(old_vcol, old_vcoh), zero);
        const __m128i new_vcch = select(update_high, greater_equal, old_vcch);

        const __m128i choose_negated = select(old_vcol, new_vccl, new_vcch);
        const __m128i result = select(choose_negated, negated_right, left);
        vccl_ = flags_from_mask(new_vccl);
        vcch_ = flags_from_mask(new_vcch);
        vcol_ = vcoh_ = vce_ = 0;
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x25: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i opposite = _mm_srai_epi16(_mm_xor_si128(left, right), 15);
        const __m128i negated_right = _mm_sub_epi16(_mm_xor_si128(right, opposite), opposite);
        const __m128i difference = _mm_sub_epi16(left, negated_right);
        const __m128i difference_zero = _mm_cmpeq_epi16(difference, zero);
        const __m128i difference_positive = _mm_cmpgt_epi16(difference, zero);
        const __m128i difference_nonnegative = _mm_or_si128(difference_positive, difference_zero);
        const __m128i difference_nonpositive = _mm_cmpeq_epi16(difference_positive, zero);
        const __m128i right_negative = _mm_srai_epi16(right, 15);

        const __m128i new_vccl = select(opposite, difference_nonpositive, right_negative);
        const __m128i new_vcch = select(opposite, right_negative, difference_nonnegative);
        const __m128i new_vce = _mm_and_si128(opposite, _mm_cmpeq_epi16(difference, _mm_set1_epi16(-1)));
        const __m128i new_vcoh = _mm_cmpeq_epi16(_mm_or_si128(difference_zero, new_vce), zero);
        const __m128i choose_negated = select(opposite, new_vccl, new_vcch);
        const __m128i result = select(choose_negated, negated_right, left);

        vcol_ = flags_from_mask(opposite);
        vcoh_ = flags_from_mask(new_vcoh);
        vccl_ = flags_from_mask(new_vccl);
        vcch_ = flags_from_mask(new_vcch);
        vce_ = flags_from_mask(new_vce);
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x26: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        const __m128i opposite = _mm_srai_epi16(_mm_xor_si128(left, right), 15);
        const __m128i right_negative = _mm_srai_epi16(right, 15);
        const __m128i sum = _mm_add_epi16(left, right);
        const __m128i difference = _mm_sub_epi16(left, right);
        const __m128i sum_negative = _mm_srai_epi16(sum, 15);
        const __m128i difference_nonnegative = _mm_cmpeq_epi16(_mm_srai_epi16(difference, 15), zero);
        const __m128i new_vccl = select(opposite, sum_negative, right_negative);
        const __m128i new_vcch = select(opposite, right_negative, difference_nonnegative);
        const __m128i alternate = _mm_xor_si128(right, opposite);
        const __m128i choose_alternate = select(opposite, new_vccl, new_vcch);
        const __m128i result = select(choose_alternate, alternate, left);

        vccl_ = flags_from_mask(new_vccl);
        vcch_ = flags_from_mask(new_vcch);
        vcol_ = vcoh_ = vce_ = 0;
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x27: {
        const Operands operands = snapshot_operands();
        const __m128i result = select(flag_mask(vccl_), operands.left, operands.right);
        vcol_ = vcoh_ = 0;
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d: {
        const Operands operands = snapshot_operands();
        const __m128i left = operands.left;
        const __m128i right = operands.right;
        __m128i result{};
        if (function < 0x2a)
            result = _mm_and_si128(left, right);
        else if (function < 0x2c)
            result = _mm_or_si128(left, right);
        else
            result = _mm_xor_si128(left, right);
        if ((function & 1U) != 0)
            result = _mm_xor_si128(result, _mm_set1_epi32(-1));
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    default:
        return false;
    }
#endif
}

} // namespace cupid
