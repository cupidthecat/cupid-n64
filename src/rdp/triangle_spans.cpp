#include "cupid/rdp.hpp"

#include <algorithm>

namespace cupid {
namespace {

s32 signed_field(u32 value, unsigned bits) {
    const u32 sign = 1U << (bits - 1U);
    const u32 field = value & ((sign << 1U) - 1U);
    return static_cast<s32>(field ^ sign) - static_cast<s32>(sign);
}

struct Edge {
    u32 position;
    u32 step;

    explicit Edge(u64 command)
        : position(static_cast<u32>(command >> 32U) & 0x0ffffffeU),
          step((static_cast<u32>(command) >> 2U) & 0x0ffffffeU) {}

    s32 sample(s32 rows) const {
        // X wraps as signed 12.16; each quarter-row step discards three slope bits.
        const u32 value = position + step * static_cast<u32>(rows);
        const s32 x = signed_field(value, 28);
        // Retain a sticky bit below the eighth-pixel edge coordinate.
        return (x >> 13) | static_cast<s32>((value & 0x1fffU) != 0);
    }
};

} // namespace

void Rdp::fill_copy_triangle(bool copy) {
    const u64 command = buffered_word(0);
    const bool left_major = (command & (1ULL << 55U)) != 0;
    const s32 top = signed_field(static_cast<u32>(command), 14);
    const s32 middle = signed_field(static_cast<u32>(command >> 16U), 14);
    const s32 bottom = signed_field(static_cast<u32>(command >> 32U), 14);
    const s32 origin = top & ~3;
    const Edge lower(buffered_word(8));
    const Edge major(buffered_word(16));
    const Edge upper(buffered_word(24));
    const RdpTextureAttributes attributes = copy ? triangle_texture_attributes() : RdpTextureAttributes{};
    const s32 first = std::max(top, static_cast<s32>(scissor_y0_));
    const s32 limit = std::min(bottom, static_cast<s32>(scissor_y1_));
    const s32 clip_left = static_cast<s32>(scissor_x0_) * 2;
    const s32 clip_right = static_cast<s32>(scissor_x1_) * 2;
    if (first >= limit || clip_left > clip_right)
        return;

    for (s32 y = first >> 2; y <= (limit - 1) >> 2; ++y) {
        if (scissor_field_enabled_ && (y & 1) != static_cast<s32>(scissor_keep_odd_))
            continue;
        s32 left = 0xffff;
        s32 right = 0;
        bool outside_left = true;
        bool outside_right = true;
        bool valid = false;
        for (s32 sub = 0; sub < 4; ++sub) {
            const s32 row = y * 4 + sub;
            const s32 x_major = major.sample(row - origin);
            // A middle edge above the first visited row never reaches its switch point.
            const s32 x_minor =
                middle >= origin && row >= middle ? lower.sample(row - middle) : upper.sample(row - origin);
            const s32 l = left_major ? x_major : x_minor;
            const s32 r = left_major ? x_minor : x_major;
            outside_left = outside_left && std::max(l, r) < clip_left;
            outside_right = outside_right && std::min(l, r) >= clip_right;
            if (row < first || row >= limit || (l >> 1) > (r >> 1))
                continue;
            valid = true;
            left = std::min(left, std::clamp(l, clip_left, clip_right));
            right = std::max(right, std::clamp(r, clip_left, clip_right));
        }
        if (valid && !outside_left && !outside_right) {
            const auto row = static_cast<unsigned>(y);
            const auto start = static_cast<unsigned>(left >> 3);
            const auto end = static_cast<unsigned>(right >> 3);
            if (copy)
                copy_triangle_span(row, start, end, attributes);
            else
                fill_span(row, start, end);
        }
    }
}

} // namespace cupid
