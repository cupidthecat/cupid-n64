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
    const unsigned function = instruction & 63U;
    switch (function) {
    case 0x00:
    case 0x01:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x14:
    case 0x15:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d:
        break;
    default:
        return false;
    }

    // Read both complete operands before committing a destination that may alias
    // either source, including a shuffled VT that is also VS and VD.
    const __m128i left = load_vector(vr_[(instruction >> 11U) & 31U].byte);
    const __m128i right = select_vector(vr_[(instruction >> 16U) & 31U].byte, (instruction >> 21U) & 15U);
    auto& destination = vr_[(instruction >> 6U) & 31U].byte;
    const __m128i zero = _mm_setzero_si128();
    const auto read_slices = [&]() -> PackedAccumulator {
        return {load_bytes(accumulator_.low.data()), load_bytes(accumulator_.middle.data()),
                load_bytes(accumulator_.high.data())};
    };
    const auto write_slices = [&](PackedAccumulator value) {
        store_bytes(accumulator_.low.data(), value.low);
        store_bytes(accumulator_.middle.data(), value.middle);
        store_bytes(accumulator_.high.data(), value.high);
    };

    switch (function) {
    case 0x10:
    case 0x11: {
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
    case 0x14: {
        const __m128i result = _mm_add_epi16(left, right);
        vcol_ = flags_from_mask(unsigned_less(result, left));
        vcoh_ = 0;
        store_bytes(accumulator_.low.data(), result);
        store_vector(destination, result);
        return true;
    }
    case 0x15: {
        const __m128i result = _mm_sub_epi16(left, right);
        vcol_ = flags_from_mask(unsigned_less(left, right));
        vcoh_ = flags_from_mask(_mm_cmpeq_epi16(_mm_cmpeq_epi16(left, right), zero));
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
        break;
    }

    PackedAccumulator product{};
    switch (function) {
    case 0x04:
    case 0x0c:
        product = {_mm_mulhi_epu16(left, right), zero, zero};
        break;
    case 0x05:
    case 0x0d: {
        const __m128i high =
            _mm_sub_epi16(_mm_mulhi_epu16(left, right), _mm_and_si128(right, _mm_srai_epi16(left, 15)));
        product = {_mm_mullo_epi16(left, right), high, _mm_srai_epi16(high, 15)};
        break;
    }
    case 0x06:
    case 0x0e: {
        const __m128i high =
            _mm_sub_epi16(_mm_mulhi_epu16(left, right), _mm_and_si128(left, _mm_srai_epi16(right, 15)));
        product = {_mm_mullo_epi16(left, right), high, _mm_srai_epi16(high, 15)};
        break;
    }
    default: {
        const __m128i high = _mm_mulhi_epi16(left, right);
        product = {_mm_mullo_epi16(left, right), high, _mm_srai_epi16(high, 15)};
        break;
    }
    }

    if (function == 0x07 || function == 0x0f) {
        product = {zero, product.low, product.middle};
    } else if (function == 0x00 || function == 0x01 || function == 0x08 || function == 0x09) {
        // Shift the signed product across all three slices before rounding or
        // accumulation. The positive 0x40000000 product needs its 33rd result bit.
        product = {_mm_slli_epi16(product.low, 1),
                   _mm_or_si128(_mm_slli_epi16(product.middle, 1), _mm_srli_epi16(product.low, 15)),
                   _mm_or_si128(_mm_slli_epi16(product.high, 1), _mm_srli_epi16(product.middle, 15))};
        if (function < 0x02)
            product = add(product, {_mm_set1_epi16(-32768), zero, zero});
    }

    const PackedAccumulator result = function >= 0x08 ? add(read_slices(), product) : product;
    write_slices(result);

    switch (function) {
    case 0x01:
    case 0x09:
        store_vector(destination, unsigned_middle(result));
        break;
    case 0x04:
    case 0x06:
        store_vector(destination, result.low);
        break;
    case 0x05:
        store_vector(destination, result.middle);
        break;
    case 0x0c:
    case 0x0e:
        store_vector(destination, unsigned_low(result));
        break;
    default:
        store_vector(destination, signed_middle(result));
        break;
    }
    return true;
#endif
}

} // namespace cupid
