#include "cupid/rdp/texture_sampling.hpp"

#include <algorithm>
#include <bit>

namespace cupid {

RdpTextureLod rdp_texture_lod(RdpTexturePoint point, RdpTexturePoint next_x, RdpTexturePoint next_y,
                              unsigned tile, unsigned maximum_level, unsigned minimum_lod, u64 modes,
                              bool overflow) {
    const bool detail = (modes & (1ULL << 50U)) != 0;
    const bool sharpen = (modes & (1ULL << 49U)) != 0;
    unsigned magnitude = 0;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const s32 dx = next_x[axis] - point[axis];
        const s32 dy = next_y[axis] - point[axis];
        magnitude = std::max(
            {magnitude, static_cast<unsigned>(dx ^ (dx >> 31)), static_cast<unsigned>(dy ^ (dy >> 31))});
    }
    bool distant = overflow || magnitude >= 16384U;
    bool magnify = false;
    unsigned level = 0;
    s32 fraction = 255;
    if (!distant) {
        magnify = magnitude < 32U;
        level = magnify ? 0U : static_cast<unsigned>(std::bit_width(magnitude >> 5U)) - 1U;
        distant = level >= maximum_level;
        if (magnify)
            fraction = detail || sharpen
                           ? static_cast<s32>(std::max(minimum_lod, magnitude) * 8U) - (sharpen ? 256 : 0)
                       : distant ? 255
                                 : 0;
        else if (detail || sharpen || !distant)
            fraction = static_cast<s32>(((magnitude << 3U) >> level) & 255U);
    }
    RdpTextureLod result{tile & 7U, (tile + 1U) & 7U, fraction};
    if ((modes & (1ULL << 48U)) != 0) {
        if (distant)
            level = maximum_level;
        if (detail) {
            result.first_tile = (tile + level + (magnify ? 0U : 1U)) & 7U;
            result.second_tile = (tile + level + (distant || magnify ? 1U : 2U)) & 7U;
        } else {
            result.first_tile = (tile + level) & 7U;
            result.second_tile =
                distant || (!sharpen && magnify) ? result.first_tile : (result.first_tile + 1U) & 7U;
        }
    }
    return result;
}

} // namespace cupid
