#include "cupid/rdp/triangle.hpp"

#include <algorithm>
#include <bit>

namespace cupid {

u32 rdp_varying_base(RdpVarying varying, RdpTriangleOrigin origin) {
    u32 offset = 0;
    if (origin.offset_latch) {
        const u32 edge = varying.de & ~511U;
        const u32 vertical = varying.dy & ~511U;
        offset = edge - static_cast<u32>(std::bit_cast<s32>(edge) >> 2) - vertical +
                 static_cast<u32>(std::bit_cast<s32>(vertical) >> 2);
    }
    const u32 step = static_cast<u32>((std::bit_cast<s32>(varying.dx) >> 8) & ~1);
    return (((varying.value + varying.de * static_cast<u32>(origin.rows)) & ~511U) + offset -
            origin.fraction * step) &
           ~1023U;
}

s32 rdp_interpolate_shade(u32 base, RdpVarying varying, s32 dx, unsigned coverage) {
    const unsigned first = static_cast<unsigned>(std::countr_zero(coverage));
    const unsigned y = first >> 1U;
    const unsigned x = (first & 1U) * 2U + (y & 1U);
    const u32 value = base + (varying.dx & ~31U) * static_cast<u32>(dx);
    const u32 snapped = static_cast<u32>(std::bit_cast<s32>(value) >> 14) * 4U +
                        x * static_cast<u32>(std::bit_cast<s32>(varying.dx) >> 14) +
                        y * static_cast<u32>(std::bit_cast<s32>(varying.dy) >> 14);
    const s32 interpolated = std::bit_cast<s16>(static_cast<u16>(snapped)) >> 4;
    const unsigned channel = static_cast<unsigned>(interpolated) & 511U;
    return static_cast<s32>(channel < 256U ? channel : channel < 384U ? 255U : 0U);
}

u32 rdp_interpolate_depth(u32 base, RdpVarying varying, s32 dx, unsigned coverage) {
    const unsigned first = static_cast<unsigned>(std::countr_zero(coverage));
    const unsigned y = first >> 1U;
    const unsigned x = (first & 1U) * 2U + (y & 1U);
    const u32 value = base + varying.dx * static_cast<u32>(dx);
    const u32 snapped = static_cast<u32>(std::bit_cast<s32>(value) >> 10) * 4U +
                        x * static_cast<u32>(std::bit_cast<s32>(varying.dx) >> 10) +
                        y * static_cast<u32>(std::bit_cast<s32>(varying.dy) >> 10);
    const u32 depth = static_cast<u32>(std::bit_cast<s32>(snapped) >> 5) & 0x7ffffU;
    return depth >= 0x60000U ? 0U : std::min(depth, 0x3ffffU);
}

u16 rdp_triangle_depth_delta(RdpVarying varying) {
    const s32 dx = std::bit_cast<s32>(varying.dx) >> 16;
    const s32 dy = std::bit_cast<s32>(varying.dy) >> 16;
    const unsigned magnitude =
        static_cast<unsigned>(dx < 0 ? ~dx & 0x7fff : dx) + static_cast<unsigned>(dy < 0 ? ~dy & 0x7fff : dy);
    return static_cast<u16>(std::min(0x8000U, std::bit_ceil(magnitude + 1U)));
}

} // namespace cupid
