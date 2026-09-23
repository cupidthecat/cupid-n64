#pragma once

#include "cupid/types.hpp"

#include <array>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include "vector_lanes.hpp"
#endif

namespace cupid::rsp_vector {

inline void load_full_vector(std::array<u16, 8>& lanes, const u8* source) {
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const __m128i bytes = load_bytes(source);
    store_vector(lanes, _mm_or_si128(_mm_slli_epi16(bytes, 8), _mm_srli_epi16(bytes, 8)));
#else
    for (unsigned lane = 0; lane < lanes.size(); ++lane)
        lanes[lane] = read_be16(source + lane * 2U);
#endif
}

inline void store_full_vector(u8* destination, const std::array<u16, 8>& lanes) {
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const __m128i bytes = load_vector(lanes);
    store_bytes(destination, _mm_or_si128(_mm_slli_epi16(bytes, 8), _mm_srli_epi16(bytes, 8)));
#else
    for (unsigned lane = 0; lane < lanes.size(); ++lane)
        write_be16(destination + lane * 2U, lanes[lane]);
#endif
}

} // namespace cupid::rsp_vector
