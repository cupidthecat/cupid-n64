#include "cupid/rdp/texture_coordinates.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

// The divider ROM uses quantized reciprocals, including its rounding irregularities.
constexpr std::array<s32, 65> reciprocals{
    0x4000, 0x3f04, 0x3e10, 0x3d22, 0x3c3c, 0x3b5d, 0x3a83, 0x39b1, 0x38e4, 0x381c, 0x375a, 0x369d, 0x35e5,
    0x3532, 0x3483, 0x33d9, 0x3333, 0x3291, 0x31f4, 0x3159, 0x30c3, 0x3030, 0x2fa1, 0x2f15, 0x2e8c, 0x2e06,
    0x2d83, 0x2d03, 0x2c86, 0x2c0b, 0x2b93, 0x2b1e, 0x2aab, 0x2a3a, 0x29cc, 0x2960, 0x28f6, 0x288e, 0x2828,
    0x27c4, 0x2762, 0x2702, 0x26a4, 0x2648, 0x25ed, 0x2594, 0x253d, 0x24e7, 0x2492, 0x243f, 0x23ee, 0x239e,
    0x234f, 0x2302, 0x22b6, 0x226c, 0x2222, 0x21da, 0x2193, 0x214d, 0x2108, 0x20c5, 0x2082, 0x2041, 0x2000};

} // namespace

s16 rdp_perspective_coordinate(s16 coordinate, s16 w) {
    bool overflow = false;
    return static_cast<s16>(
        std::clamp(rdp_perspective_coordinate_wide(coordinate, w, overflow), -0x8000, 0x7fff));
}

s32 rdp_perspective_coordinate_wide(s16 coordinate, s16 w, bool& overflow) {
    if (w <= 0) {
        overflow = true;
        return 0x7fff;
    }
    const unsigned shift = 15U - static_cast<unsigned>(std::bit_width(static_cast<u32>(w)));
    const unsigned normalized = (static_cast<u32>(w) << shift) & 0x3fffU;
    const unsigned index = normalized >> 8U;
    const s32 fraction = static_cast<s32>(normalized & 0xffU);
    const s32 reciprocal =
        reciprocals[index] + (((reciprocals[index + 1] - reciprocals[index]) * fraction) >> 8);
    const s32 product = static_cast<s32>(coordinate) * reciprocal;
    const s32 divided = shift == 14U ? product * 2 : product >> (13U - shift);
    const u32 mask = 0x3fffffffU & (0U - (1U << (29U - shift)));
    const u32 outside = static_cast<u32>(product) & mask;
    if (outside != 0 && outside != mask) {
        overflow = true;
        return ((shift == 14U ? product : divided) & (1 << 29)) != 0 ? -0x8000 : 0x7fff;
    }
    return std::clamp(divided, -0x10000, 0xffff);
}

} // namespace cupid
