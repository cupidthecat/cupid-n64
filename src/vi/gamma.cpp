#include "cupid/vi/filter.hpp"

#include <algorithm>

namespace cupid {
namespace {
constexpr u32 gamma_root(u32 value) {
    u32 root = 0;
    for (u32 bit = 64; bit != 0; bit >>= 1) {
        const u32 candidate = root | bit;
        if (candidate * candidate <= value)
            root = candidate;
    }
    return root * 2;
}

const auto gamma_table = [] {
    std::array<u8, 16384> table{};
    for (unsigned value = 0; value < table.size(); ++value)
        table[value] = static_cast<u8>(gamma_root(value));
    return table;
}();
} // namespace

ViColor vi_gamma(ViColor color, bool gamma, bool dither, u16 noise) {
    const std::array<u32, 3> random = {noise & 63U, (noise >> 6) & 63U, ((noise >> 9) & 56U) | (noise & 7U)};
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (gamma)
            color[channel] = gamma_table[(color[channel] << 6) | (dither ? random[channel] : 0U)];
        else if (dither)
            color[channel] = std::min<u32>(255, color[channel] + ((noise >> channel) & 1U));
    }
    return color;
}

u16 vi_gamma_noise(u32 field, unsigned x, unsigned y) {
    // Spatial noise is repeatable within a field; the hardware clock phase is not reconstructed.
    u32 value = field * 0x9e3779b9U + x * 0x85ebca6bU + y * 0xc2b2ae35U + 0x56490000U;
    value = (value ^ (value >> 16)) * 0x7feb352dU;
    value = (value ^ (value >> 15)) * 0x846ca68bU;
    return static_cast<u16>((value ^ (value >> 16)) >> 16);
}

} // namespace cupid
