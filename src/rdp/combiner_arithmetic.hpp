#pragma once

#include "cupid/rdp/color_pipeline.hpp"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

namespace cupid::rdp_combiner {

inline bool evaluate_rgb_product(RdpColor& color, const RdpColor& a, const RdpColor& b, const RdpColor& c,
                                 const RdpColor& d) {
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const auto mask = _mm_set1_epi32(511);
    const auto bias = _mm_set1_epi32(128);
    const auto sign = _mm_set1_epi32(256);
    const auto zero = _mm_setzero_si128();
    const auto expanded = [&](const RdpColor& source) {
        const auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source.data()));
        return _mm_sub_epi32(_mm_and_si128(_mm_add_epi32(value, bias), mask), bias);
    };
    // Expanded differences fit signed 10 bits; multipliers fit signed 9 bits.
    // Interleaved zero lanes make each packed multiply-add one exact product.
    const auto difference = _mm_sub_epi32(expanded(a), expanded(b));
    const auto multiplier = _mm_sub_epi32(
        _mm_xor_si128(_mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(c.data())), mask), sign),
        sign);
    const auto differences16 = _mm_packs_epi32(difference, zero);
    const auto multipliers16 = _mm_packs_epi32(multiplier, zero);
    const auto product =
        _mm_madd_epi16(_mm_unpacklo_epi16(differences16, zero), _mm_unpacklo_epi16(multipliers16, zero));
    const auto value = _mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(product, bias), 8), expanded(d));
    // Alpha is resolved separately after this RGB equation.
    _mm_storeu_si128(reinterpret_cast<__m128i*>(color.data()), value);
    return true;
#else
    static_cast<void>(color);
    static_cast<void>(a);
    static_cast<void>(b);
    static_cast<void>(c);
    static_cast<void>(d);
    return false;
#endif
}

} // namespace cupid::rdp_combiner
