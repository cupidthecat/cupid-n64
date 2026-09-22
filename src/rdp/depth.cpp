#include "cupid/rdp/depth.hpp"

#include <algorithm>
#include <bit>

namespace cupid {

u16 rdp_compress_depth(u32 depth) {
    depth &= 0x3ffffU;
    const unsigned exponent =
        std::min(7U, 18U - static_cast<unsigned>(std::bit_width(std::max(0x3ffffU - depth, 1U))));
    const unsigned shift = exponent < 6U ? 6U - exponent : 0U;
    return static_cast<u16>((exponent << 11U) | ((depth >> shift) & 0x7ffU));
}

u32 rdp_decompress_depth(u16 compressed) {
    const unsigned exponent = (compressed >> 11U) & 7U;
    const unsigned shift = exponent < 6U ? 6U - exponent : 0U;
    return 0x40000U - (0x40000U >> exponent) + ((compressed & 0x7ffU) << shift);
}

unsigned rdp_compress_depth_delta(u16 delta) {
    // Each output bit combines alternating groups of input bits.
    return ((delta & 0xff00U) != 0 ? 8U : 0U) | ((delta & 0xf0f0U) != 0 ? 4U : 0U) |
           ((delta & 0xccccU) != 0 ? 2U : 0U) | ((delta & 0xaaaaU) != 0 ? 1U : 0U);
}

RdpDepthResult rdp_test_depth(RdpDepth depth, u16 stored, u8 hidden, unsigned coverage,
                              unsigned memory_coverage, u64 modes) {
    RdpDepthResult result;
    result.coverage = coverage;
    result.coverage_wrap = coverage + memory_coverage >= 8U;
    const bool force_blend = (modes & (1ULL << 14U)) != 0;
    const bool antialias = (modes & (1ULL << 3U)) != 0;
    const unsigned delta_code = rdp_compress_depth_delta(depth.delta);
    if ((modes & (1ULL << 4U)) == 0) {
        result.memory_alpha_shift = std::min(15U - delta_code, 4U);
        result.blend_enabled = force_blend || (antialias && !result.coverage_wrap);
        return result;
    }

    const u16 memory_code = stored >> 2U;
    const unsigned memory_delta_code = ((stored & 3U) << 2U) | (hidden & 3U);
    const s32 memory_depth = static_cast<s32>(rdp_decompress_depth(memory_code));
    unsigned memory_delta = 1U << memory_delta_code;
    if (delta_code > memory_delta_code)
        result.pixel_alpha_shift = std::min(delta_code - memory_delta_code, 4U);
    else
        result.memory_alpha_shift = std::min(memory_delta_code - delta_code, 4U);

    const unsigned precision = memory_code >> 11U;
    const bool coplanar = precision < 3U && memory_delta == 0x8000U;
    if (precision < 3U)
        memory_delta = coplanar ? 0xffffU : std::max(memory_delta * 2U, 16U >> precision);
    const unsigned delta = std::bit_floor(static_cast<unsigned>(depth.delta) | memory_delta);
    const s32 tolerance = static_cast<s32>(delta * 8U);
    const s32 incoming = static_cast<s32>(depth.value & 0x3ffffU);
    const bool farther = coplanar || incoming + tolerance >= memory_depth;
    const bool nearer = coplanar || incoming - tolerance <= memory_depth;
    const bool front = incoming < memory_depth;
    const bool clear = memory_depth == 0x3ffff;
    result.blend_enabled = force_blend || (antialias && !result.coverage_wrap && farther);

    switch ((modes >> 10U) & 3U) {
    case 0:
        result.pass = clear || (result.coverage_wrap ? front : nearer);
        break;
    case 1:
        if (front && farther && result.coverage_wrap) {
            const unsigned shift = static_cast<unsigned>(std::bit_width(delta)) - 1U;
            const unsigned coefficient =
                (static_cast<unsigned>(memory_depth >> shift) - static_cast<unsigned>(incoming >> shift)) &
                15U;
            result.coverage = std::min(8U, coefficient * coverage / 8U);
        } else {
            result.pass = clear || (result.coverage_wrap ? front : nearer);
        }
        break;
    case 2:
        result.pass = front || clear;
        break;
    case 3:
        result.pass = farther && nearer && !clear;
        break;
    }
    return result;
}

} // namespace cupid
