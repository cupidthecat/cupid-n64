#pragma once

#include "cupid/rdp/color_pipeline.hpp"

namespace cupid::rdp_texture {
const std::array<u32, 65536> rgba16_colors = [] {
    std::array<u32, 65536> colors{};
    for (unsigned value = 0; value < colors.size(); ++value) {
        const unsigned red = (value >> 11U) & 31U;
        const unsigned green = (value >> 6U) & 31U;
        const unsigned blue = (value >> 1U) & 31U;
        colors[value] = ((red << 3U) | (red >> 2U)) | (((green << 3U) | (green >> 2U)) << 8U) |
                        (((blue << 3U) | (blue >> 2U)) << 16U) | ((value & 1U) * 255U << 24U);
    }
    return colors;
}();
inline RdpColor decode_rgba16(u16 value) {
    const u32 color = rgba16_colors[value];
    return {static_cast<s32>(color & 255U), static_cast<s32>((color >> 8U) & 255U),
            static_cast<s32>((color >> 16U) & 255U), static_cast<s32>(color >> 24U)};
}

} // namespace cupid::rdp_texture
