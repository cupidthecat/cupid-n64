#pragma once

#include <smmintrin.h>

namespace cupid::n64::simd {

struct Quantized {
  __m128i middle, high, result;
};

inline Quantized quantize(__m128i first, __m128i second) {
  const auto mask = _mm_set1_epi32(0xffff);
  const auto middle = _mm_packus_epi32(_mm_and_si128(first, mask), _mm_and_si128(second, mask));
  const auto high = _mm_packs_epi32(_mm_srai_epi32(first, 16), _mm_srai_epi32(second, 16));
  const auto result = _mm_and_si128(
      _mm_packs_epi32(_mm_srai_epi32(first, 1), _mm_srai_epi32(second, 1)), _mm_set1_epi16(-16));
  return {middle, high, result};
}

inline __m128i round_product(__m128i product) {
  return _mm_add_epi32(product, _mm_and_si128(_mm_srai_epi32(product, 31), _mm_set1_epi32(31)));
}

inline __m128i round_accumulator(__m128i product) {
  const auto clear =
      _mm_cmpeq_epi32(_mm_and_si128(product, _mm_set1_epi32(32)), _mm_setzero_si128());
  const auto negative = _mm_and_si128(_mm_srai_epi32(product, 31), _mm_set1_epi32(32));
  const auto positive =
      _mm_and_si128(_mm_cmpgt_epi32(product, _mm_set1_epi32(31)), _mm_set1_epi32(-32));
  return _mm_add_epi32(product, _mm_and_si128(clear, _mm_or_si128(negative, positive)));
}

} // namespace cupid::n64::simd
