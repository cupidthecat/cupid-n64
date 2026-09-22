#pragma once

#include "cupid/rdp/color_pipeline.hpp"
#include "cupid/rdp/tile.hpp"

namespace cupid {

using RdpTexturePoint = std::array<s32, 2>;

struct RdpTextureCoordinates {
    RdpTexturePoint point{};
    RdpTexturePoint next_x{};
    RdpTexturePoint next_y{};
    RdpTexturePoint next_pixel{};
    bool overflow{};
};

struct RdpTextureLod {
    unsigned first_tile{};
    unsigned second_tile{};
    s32 fraction{};
};

[[nodiscard]] RdpColor rdp_sample_texture(const std::array<u8, 4096>& memory, const RdpTile& tile,
                                          RdpTexturePoint point, u64 modes, unsigned cycle,
                                          const std::array<u16, 6>& convert, const RdpColor& previous = {});
[[nodiscard]] RdpTextureLod rdp_texture_lod(RdpTexturePoint point, RdpTexturePoint next_x,
                                            RdpTexturePoint next_y, unsigned tile, unsigned maximum_level,
                                            unsigned minimum_lod, u64 modes, bool overflow);

} // namespace cupid
