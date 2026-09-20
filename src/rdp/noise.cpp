#include "cupid/rdp/noise.hpp"

namespace cupid {

u16 rdp_pixel_noise(u32 primitive, unsigned x, unsigned y) {
    // Reproducible spatial noise; this does not model the hardware generator's clock phase.
    u32 value = primitive * 0x9e3779b9U + x * 0x85ebca6bU + y * 0xc2b2ae35U;
    value = (value ^ (value >> 16U)) * 0x7feb352dU;
    value = (value ^ (value >> 15U)) * 0x846ca68bU;
    return static_cast<u16>((value ^ (value >> 16U)) >> 16U);
}

s32 rdp_combiner_noise(u16 sample) {
    return static_cast<s32>(((sample & 7U) << 6U) | 32U);
}

std::array<unsigned, 4> rdp_dither_coefficients(u64 modes, unsigned x, unsigned y, u16 sample) {
    constexpr unsigned matrix[2][16] = {
        {0, 6, 1, 7, 4, 2, 5, 3, 3, 5, 2, 4, 7, 1, 6, 0},
        {0, 4, 1, 5, 4, 0, 5, 1, 3, 7, 2, 6, 7, 3, 6, 2},
    };
    const unsigned rgb = static_cast<unsigned>(modes >> 38U) & 3U;
    const unsigned alpha = static_cast<unsigned>(modes >> 36U) & 3U;
    const unsigned threshold = matrix[rgb & 1U][(y & 3U) * 4U + (x & 3U)];
    std::array<unsigned, 4> result{};
    for (unsigned channel = 0; channel < 3; ++channel)
        result[channel] = rgb < 2U ? threshold : rgb == 2U ? (sample >> (channel * 3U)) & 7U : 7U;
    result[3] = alpha < 2U ? threshold ^ (alpha * 7U) : alpha == 2U ? sample & 7U : 0U;
    return result;
}

} // namespace cupid
