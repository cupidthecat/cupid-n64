#include "cupid/rdp.hpp"
#include "cupid/rdp/triangle.hpp"

#include <algorithm>

namespace cupid {

void Rdp::fill_copy_triangle(bool copy) {
    const auto geometry =
        rdp_triangle_geometry({buffered_word(0), buffered_word(8), buffered_word(16), buffered_word(24)});
    const std::array<unsigned, 4> scissor{scissor_x0_, scissor_y0_, scissor_x1_, scissor_y1_};
    const s32 first = std::max(geometry.top, static_cast<s32>(scissor_y0_));
    const s32 limit = std::min(geometry.bottom, static_cast<s32>(scissor_y1_));
    if (first >= limit)
        return;
    const auto attributes = copy ? triangle_texture_attributes() : RdpTextureAttributes{};
    for (unsigned y = static_cast<unsigned>(first / 4); y <= static_cast<unsigned>((limit - 1) / 4); ++y) {
        if (scissor_field_enabled_ && (y & 1U) != static_cast<unsigned>(scissor_keep_odd_))
            continue;
        const auto span = rdp_triangle_span(geometry, scissor, y);
        if (!span.valid)
            continue;
        if (copy)
            copy_triangle_span(y, span.start, span.end, attributes);
        else
            fill_span(y, span.start, span.end);
    }
}

} // namespace cupid
