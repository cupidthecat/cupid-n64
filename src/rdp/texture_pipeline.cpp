#include "cupid/rdp.hpp"

namespace cupid {

RdpColorInputs Rdp::sample_color_textures(const RdpTextureCoordinates& coordinates, unsigned tile,
                                          unsigned inputs, unsigned maximum_level) const {
    const bool two_cycles = ((other_modes_ >> 52U) & 3U) == 1U;
    const bool convert = (other_modes_ & (1ULL << 41U)) != 0;
    RdpTextureLod lod{tile, (tile + 1U) & 7U, 0};
    if ((inputs & 4U) != 0)
        lod = rdp_texture_lod(coordinates.point, coordinates.next_x, coordinates.next_y, tile, maximum_level,
                              color_state_.minimum_lod, other_modes_, coordinates.overflow);
    RdpColorInputs result;
    result.lod_fraction = lod.fraction;
    if ((inputs & 1U) != 0 || ((inputs & 2U) != 0 && convert))
        result.texel0 = rdp_sample_texture(texture_memory_, tiles_[lod.first_tile], coordinates.point,
                                           other_modes_, 0, color_state_.convert);
    if ((inputs & 2U) != 0) {
        const auto point = two_cycles ? coordinates.point : coordinates.next_pixel;
        const unsigned second = two_cycles ? lod.second_tile : lod.first_tile;
        result.texel1 = rdp_sample_texture(texture_memory_, tiles_[second], point, other_modes_, 1,
                                           color_state_.convert, result.texel0);
    }
    return result;
}

} // namespace cupid
