#pragma once

#include "cupid/vi/filter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

#if !defined(CUPID_VI_FORCE_SCALAR) &&                                                                       \
    (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define CUPID_VI_SSE2 1
#endif

namespace cupid::vi {

#if defined(CUPID_VI_SSE2)
inline __m128i load_pixel(const ViPixel& pixel) {
    static_assert(std::is_standard_layout_v<ViPixel> && sizeof(ViPixel) == 16U);
    static_assert(offsetof(ViPixel, coverage) == 12U);
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(&pixel));
}

inline __m128i load_color(const ViColor& color) {
    return _mm_setr_epi32(static_cast<int>(color[0]), static_cast<int>(color[1]), static_cast<int>(color[2]),
                          0);
}

inline ViColor store_color(__m128i value) {
    std::array<u32, 4> channels{};
    _mm_storeu_si128(reinterpret_cast<__m128i*>(channels.data()), value);
    return {channels[0], channels[1], channels[2]};
}
#endif

inline ViColor reconstruct_edges(const ViPixel& center, std::span<const ViPixel, 6> neighbors) {
#if defined(CUPID_VI_SSE2)
    // RGB components are bytes stored in separate 32-bit lanes. Byte min/max
    // therefore selects each channel without requiring SSE4.1.
    const auto original = load_pixel(center);
    auto low = original;
    auto high = original;
    auto second_low = original;
    auto second_high = original;
    for (const auto& neighbor : neighbors) {
        if (neighbor.coverage != 7U)
            continue;
        const auto value = load_pixel(neighbor);
        second_low = _mm_min_epu8(second_low, _mm_max_epu8(value, low));
        second_high = _mm_max_epu8(second_high, _mm_min_epu8(value, high));
        low = _mm_min_epu8(low, value);
        high = _mm_max_epu8(high, value);
    }
    const auto correction =
        _mm_sub_epi32(_mm_add_epi32(second_low, second_high), _mm_slli_epi32(original, 1));
    // The correction is at most 510 in magnitude; scaling by up to seven
    // fits signed 16 bits, including the hardware rounding bias.
    const auto scaled = _mm_mullo_epi16(_mm_packs_epi32(correction, _mm_setzero_si128()),
                                        _mm_set1_epi16(static_cast<short>(7U - center.coverage)));
    const auto rounded = _mm_srai_epi16(_mm_add_epi16(scaled, _mm_set1_epi16(4)), 3);
    const auto delta = _mm_unpacklo_epi16(rounded, _mm_srai_epi16(rounded, 15));
    return store_color(_mm_and_si128(_mm_add_epi32(original, delta), _mm_set1_epi32(255)));
#else
    ViColor result{};
    for (unsigned channel = 0; channel < 3U; ++channel) {
        u32 low = center.color[channel];
        u32 high = low;
        u32 second_low = low;
        u32 second_high = low;
        for (const auto& neighbor : neighbors) {
            if (neighbor.coverage != 7U)
                continue;
            const u32 value = neighbor.color[channel];
            second_low = std::min(second_low, std::max(value, low));
            second_high = std::max(second_high, std::min(value, high));
            low = std::min(low, value);
            high = std::max(high, value);
        }
        const s32 correction =
            static_cast<s32>(second_low + second_high) - 2 * static_cast<s32>(center.color[channel]);
        const s32 delta = (correction * static_cast<s32>(7U - center.coverage) + 4) >> 3;
        result[channel] = static_cast<u32>(static_cast<s32>(center.color[channel]) + delta) & 255U;
    }
    return result;
#endif
}

class DitherRestoration {
  public:
#if defined(CUPID_VI_SSE2)
    explicit DitherRestoration(ViColor original)
        : original_(load_color(original)), quantized_(_mm_srli_epi32(original_, 3)),
          adjustment_(_mm_setzero_si128()) {}
#else
    explicit DitherRestoration(ViColor original) : original_(original) {}
#endif

    void add(const ViPixel& neighbor) {
#if defined(CUPID_VI_SSE2)
        const auto quantized = _mm_srli_epi32(load_pixel(neighbor), 3);
        adjustment_ = _mm_add_epi32(adjustment_, _mm_cmpgt_epi32(quantized_, quantized));
        adjustment_ = _mm_sub_epi32(adjustment_, _mm_cmpgt_epi32(quantized, quantized_));
#else
        for (unsigned channel = 0; channel < 3U; ++channel) {
            const s32 difference =
                static_cast<s32>(neighbor.color[channel] >> 3U) - static_cast<s32>(original_[channel] >> 3U);
            adjustment_[channel] += std::clamp<s32>(difference, -1, 1);
        }
#endif
    }

    [[nodiscard]] ViColor color() const {
#if defined(CUPID_VI_SSE2)
        const auto restored = _mm_add_epi32(_mm_and_si128(original_, _mm_set1_epi32(248)), adjustment_);
        const auto packed = _mm_packs_epi32(restored, _mm_setzero_si128());
        const auto clamped = _mm_min_epi16(_mm_max_epi16(packed, _mm_setzero_si128()), _mm_set1_epi16(255));
        return store_color(_mm_unpacklo_epi16(clamped, _mm_setzero_si128()));
#else
        ViColor result{};
        for (unsigned channel = 0; channel < 3U; ++channel)
            result[channel] = static_cast<u32>(
                std::clamp<s32>(static_cast<s32>(original_[channel] & 248U) + adjustment_[channel], 0, 255));
        return result;
#endif
    }

  private:
#if defined(CUPID_VI_SSE2)
    __m128i original_, quantized_, adjustment_;
#else
    ViColor original_;
    std::array<s32, 3> adjustment_{};
#endif
};

} // namespace cupid::vi
