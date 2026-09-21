#include "cupid/rdp/triangle.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

s32 signed_field(u32 value, unsigned bits) {
    const u32 sign = 1U << (bits - 1U);
    return static_cast<s32>((value & ((sign << 1U) - 1U)) ^ sign) - static_cast<s32>(sign);
}

s32 sample_edge(RdpTriangleEdge edge, s32 rows) {
    const u32 value = edge.position + edge.step * static_cast<u32>(rows);
    return (signed_field(value, 28) >> 13) | static_cast<s32>((value & 0x1fffU) != 0);
}

} // namespace

RdpTriangleGeometry rdp_triangle_geometry(const std::array<u64, 4>& words) {
    const auto edge = [](u64 word) {
        return RdpTriangleEdge{static_cast<u32>(word >> 32U) & 0x0ffffffeU,
                               (static_cast<u32>(word) >> 2U) & 0x0ffffffeU};
    };
    const bool left_major = (words[0] & (1ULL << 55U)) != 0;
    return {signed_field(static_cast<u32>(words[0]), 14),
            signed_field(static_cast<u32>(words[0] >> 16U), 14),
            signed_field(static_cast<u32>(words[0] >> 32U), 14),
            left_major,
            left_major == ((words[2] & 0x80000000U) != 0),
            edge(words[1]),
            edge(words[2]),
            edge(words[3])};
}

RdpTriangleSpan rdp_triangle_span(const RdpTriangleGeometry& geometry, const std::array<unsigned, 4>& scissor,
                                  unsigned y) {
    RdpTriangleSpan result;
    const s32 origin = geometry.top & ~3;
    const s32 first = std::max(geometry.top, static_cast<s32>(scissor[1]));
    const s32 limit = std::min(geometry.bottom, static_cast<s32>(scissor[3]));
    const s32 clip_left = static_cast<s32>(scissor[0]) * 2;
    const s32 clip_right = static_cast<s32>(scissor[2]) * 2;
    if (first >= limit || clip_left > clip_right)
        return result;
    bool outside_left = true, outside_right = true;
    s32 left = 65535, right = 0;
    for (unsigned sub = 0; sub < 4; ++sub) {
        const s32 row = static_cast<s32>(y * 4U + sub);
        const s32 major = sample_edge(geometry.major, row - origin);
        const s32 minor = row >= geometry.middle ? sample_edge(geometry.lower, row - geometry.middle)
                                                 : sample_edge(geometry.upper, row - origin);
        const s32 l = geometry.left_major ? major : minor;
        const s32 r = geometry.left_major ? minor : major;
        outside_left = outside_left && std::max(l, r) < clip_left;
        outside_right = outside_right && std::min(l, r) >= clip_right;
        if (row < first || row >= limit || (l >> 1) > (r >> 1))
            continue;
        result.valid = true;
        result.left[sub] = std::clamp(l, clip_left, clip_right);
        result.right[sub] = std::clamp(r, clip_left, clip_right);
        left = std::min(left, result.left[sub]);
        right = std::max(right, result.right[sub]);
    }
    result.valid = result.valid && !outside_left && !outside_right;
    if (result.valid) {
        result.start = static_cast<unsigned>(left >> 3);
        result.end = static_cast<unsigned>(right >> 3);
    }
    return result;
}

unsigned rdp_triangle_coverage(const RdpTriangleSpan& span, unsigned x) {
    unsigned coverage = 0;
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned sample = 0; sample < 2; ++sample) {
            const s32 position = static_cast<s32>(x * 8U + (row & 1U) * 2U + sample * 4U);
            if (position >= span.left[row] && position < span.right[row])
                coverage |= 1U << (row * 2U + sample);
        }
    }
    return coverage;
}

RdpTriangleOrigin rdp_triangle_origin(const RdpTriangleGeometry& geometry, unsigned y) {
    const s32 rows = static_cast<s32>(y) - (geometry.top >> 2);
    const u32 position = static_cast<u32>(signed_field(geometry.major.position, 28) >> 1);
    const u32 step = static_cast<u32>(signed_field(geometry.major.step, 28) >> 1);
    const u32 x = position + step * (static_cast<u32>(rows) * 4U + (geometry.offset_latch ? 3U : 0U));
    return {std::bit_cast<s32>(x) >> 15, rows, (x >> 7U) & 255U, geometry.offset_latch};
}

} // namespace cupid
