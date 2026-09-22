#include "cupid/rdp/color_pipeline.hpp"

namespace cupid {

RdpColor rdp_blend(const RdpColorState& state, u64 modes, RdpColor pixel, const RdpColor& memory,
                   unsigned shade_alpha, bool blend_enabled, bool coverage_wrap, unsigned memory_alpha_shift,
                   unsigned pixel_alpha_shift) {
    const bool two_cycles = ((modes >> 52U) & 3U) == 1U;
    const bool force = (modes & (1ULL << 14U)) != 0;
    const RdpColor fog = rdp_unpack_color(state.fog);
    const RdpColor blend = rdp_unpack_color(state.blend);
    for (unsigned cycle = 0; cycle < (two_cycles ? 2U : 1U); ++cycle) {
        const bool final = !two_cycles || cycle == 1U;
        const std::array<RdpColor, 4> colors = {pixel, memory, blend, fog};
        const unsigned p = static_cast<unsigned>(modes >> (30U - cycle * 2U)) & 3U;
        const unsigned a = static_cast<unsigned>(modes >> (26U - cycle * 2U)) & 3U;
        const unsigned m = static_cast<unsigned>(modes >> (22U - cycle * 2U)) & 3U;
        const unsigned b = static_cast<unsigned>(modes >> (18U - cycle * 2U)) & 3U;
        if (final && (modes & (1ULL << 7U)) != 0 && !coverage_wrap) {
            for (unsigned channel = 0; channel < 3; ++channel)
                pixel[channel] = colors[m][channel];
            break;
        }
        if (final && (!blend_enabled || (a == 0U && b == 0U && pixel[3] == 255))) {
            for (unsigned channel = 0; channel < 3; ++channel)
                pixel[channel] = colors[p][channel];
            break;
        }
        const unsigned factors[4] = {static_cast<unsigned>(pixel[3]), static_cast<unsigned>(fog[3]),
                                     shade_alpha, 0};
        unsigned first = factors[a];
        const unsigned complements[4] = {first ^ 255U, static_cast<unsigned>(memory[3]), 255U, 0};
        unsigned second = complements[b] >> 3U;
        first >>= 3U;
        if (b == 1U) {
            first = (first >> pixel_alpha_shift) & 60U;
            second = (second >> memory_alpha_shift) | 3U;
        }
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned weighted = static_cast<unsigned>(colors[p][channel]) * first +
                                      static_cast<unsigned>(colors[m][channel]) * (second + 1U);
            pixel[channel] = !final || force
                                 ? static_cast<s32>((weighted >> 5U) & 255U)
                                 : rdp_blend_divide(weighted >> 2U, (first >> 2U) + (second >> 2U) + 1U);
        }
    }
    return pixel;
}

} // namespace cupid
