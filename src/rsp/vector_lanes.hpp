#pragma once

#include "cupid/types.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <emmintrin.h>

namespace cupid::rsp_vector {

struct Wide32 {
    __m128i low;
    __m128i high;
};

struct Accumulator {
    __m128i low;
    __m128i middle;
    __m128i high;
};

inline __m128i load_bytes(const void* source) {
    __m128i value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

inline void store_bytes(void* destination, __m128i value) {
    std::memcpy(destination, &value, sizeof(value));
}

inline __m128i load_vector(const std::array<u16, 8>& lanes) {
    return load_bytes(lanes.data());
}

inline void store_vector(std::array<u16, 8>& lanes, __m128i value) {
    store_bytes(lanes.data(), value);
}

template <int Selection> inline __m128i shuffle_halves(__m128i value) {
    return _mm_shufflehi_epi16(_mm_shufflelo_epi16(value, Selection), Selection);
}

inline __m128i select_vector(const std::array<u16, 8>& lanes, unsigned element) {
    if (element >= 8U)
        return _mm_set1_epi16(std::bit_cast<s16>(lanes[element - 8U]));
    const __m128i value = load_vector(lanes);
    switch (element) {
    case 2:
        return shuffle_halves<_MM_SHUFFLE(2, 2, 0, 0)>(value);
    case 3:
        return shuffle_halves<_MM_SHUFFLE(3, 3, 1, 1)>(value);
    case 4:
        return shuffle_halves<_MM_SHUFFLE(0, 0, 0, 0)>(value);
    case 5:
        return shuffle_halves<_MM_SHUFFLE(1, 1, 1, 1)>(value);
    case 6:
        return shuffle_halves<_MM_SHUFFLE(2, 2, 2, 2)>(value);
    case 7:
        return shuffle_halves<_MM_SHUFFLE(3, 3, 3, 3)>(value);
    default:
        return value;
    }
}

inline Wide32 widen_signed(__m128i value) {
    const __m128i sign = _mm_srai_epi16(value, 15);
    return {_mm_unpacklo_epi16(value, sign), _mm_unpackhi_epi16(value, sign)};
}

inline Wide32 widen_unsigned(__m128i value) {
    const __m128i zero = _mm_setzero_si128();
    return {_mm_unpacklo_epi16(value, zero), _mm_unpackhi_epi16(value, zero)};
}

inline __m128i unsigned_less(__m128i left, __m128i right) {
    const __m128i sign = _mm_set1_epi16(-32768);
    return _mm_cmpgt_epi16(_mm_xor_si128(right, sign), _mm_xor_si128(left, sign));
}

inline Accumulator add(Accumulator left, Accumulator right) {
    const __m128i low = _mm_add_epi16(left.low, right.low);
    const __m128i carry_low = unsigned_less(low, left.low);
    const __m128i middle_sum = _mm_add_epi16(left.middle, right.middle);
    const __m128i middle = _mm_sub_epi16(middle_sum, carry_low);
    const __m128i carry_middle =
        _mm_or_si128(unsigned_less(middle_sum, left.middle), unsigned_less(middle, middle_sum));
    const __m128i high = _mm_sub_epi16(_mm_add_epi16(left.high, right.high), carry_middle);
    return {low, middle, high};
}

inline __m128i signed_middle(Accumulator value) {
    return _mm_packs_epi32(_mm_unpacklo_epi16(value.middle, value.high),
                           _mm_unpackhi_epi16(value.middle, value.high));
}

inline __m128i unsigned_middle(Accumulator value) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i negative = _mm_srai_epi16(value.high, 15);
    const __m128i overflow = _mm_or_si128(_mm_cmpeq_epi16(_mm_cmpeq_epi16(value.high, zero), zero),
                                          _mm_srai_epi16(value.middle, 15));
    return _mm_andnot_si128(negative, _mm_or_si128(value.middle, overflow));
}

inline __m128i unsigned_low(Accumulator value) {
    const __m128i fits = _mm_cmpeq_epi16(value.high, _mm_srai_epi16(value.middle, 15));
    const __m128i saturated = _mm_cmpeq_epi16(_mm_srai_epi16(value.high, 15), _mm_setzero_si128());
    return _mm_or_si128(_mm_and_si128(fits, value.low), _mm_andnot_si128(fits, saturated));
}

inline __m128i flag_values(u8 flags) {
    const __m128i bits = _mm_set_epi16(128, 64, 32, 16, 8, 4, 2, 1);
    const __m128i selected = _mm_and_si128(_mm_set1_epi16(static_cast<short>(flags)), bits);
    return _mm_and_si128(_mm_cmpeq_epi16(selected, bits), _mm_set1_epi16(1));
}

inline u8 flags_from_mask(__m128i mask) {
    return static_cast<u8>(_mm_movemask_epi8(_mm_packs_epi16(mask, _mm_setzero_si128())));
}

} // namespace cupid::rsp_vector
