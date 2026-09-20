#include "cupid/rdp/color_pipeline.hpp"

#include <algorithm>

namespace cupid {
namespace {

s32 signed_nine(s32 value) {
    return static_cast<s32>((static_cast<u32>(value) & 511U) ^ 256U) - 256;
}

s32 expand(s32 value) {
    return signed_nine(value - 128) + 128;
}

s32 clamp_color(s32 value) {
    return std::clamp(expand(value), 0, 255);
}

RdpColor combine_cycle(const RdpColorState& state, const RdpColorInputs& inputs, const RdpColor& combined,
                       unsigned cycle) {
    constexpr unsigned rgb_shifts[2][4] = {{52, 28, 47, 15}, {37, 24, 32, 6}};
    constexpr unsigned alpha_shifts[2][4] = {{44, 12, 41, 9}, {21, 3, 18, 0}};
    constexpr unsigned rgb_masks[4] = {15, 15, 31, 7};
    const std::array<RdpColor, 6> colors = {combined,      inputs.texel0,
                                            inputs.texel1, rdp_unpack_color(state.primitive),
                                            inputs.shade,  rdp_unpack_color(state.environment)};
    RdpColor result{};
    for (unsigned channel = 0; channel < 4; ++channel) {
        std::array<s32, 4> terms{};
        for (unsigned term = 0; term < 4; ++term) {
            if (channel == 3U) {
                const unsigned selector =
                    static_cast<unsigned>(state.combine >> alpha_shifts[cycle][term]) & 7U;
                if (term == 2U) {
                    if (selector == 0U)
                        terms[term] = inputs.lod_fraction;
                    else if (selector < 6U)
                        terms[term] = colors[selector][3];
                    else if (selector == 6U)
                        terms[term] = state.primitive_lod;
                } else if (selector < 6U)
                    terms[term] = colors[selector][3];
                else if (selector == 6U)
                    terms[term] = 256;
            } else {
                const unsigned selector =
                    static_cast<unsigned>(state.combine >> rgb_shifts[cycle][term]) & rgb_masks[term];
                if (selector < 6U)
                    terms[term] = colors[selector][channel];
                else if (term == 2U) {
                    if (selector == 6U)
                        terms[term] = state.key_scale[channel];
                    else if (selector <= 12U)
                        terms[term] = colors[selector - 7U][3];
                    else if (selector == 13U)
                        terms[term] = inputs.lod_fraction;
                    else if (selector == 14U)
                        terms[term] = state.primitive_lod;
                    else if (selector == 15U)
                        terms[term] = state.convert[5];
                } else if (term == 1U) {
                    if (selector == 6U)
                        terms[term] = state.key_center[channel];
                    else if (selector == 7U)
                        terms[term] = state.convert[4];
                } else if (selector == 6U)
                    terms[term] = 256;
                else if (term == 0U && selector == 7U)
                    terms[term] = inputs.noise[cycle];
            }
        }
        result[channel] =
            (((expand(terms[0]) - expand(terms[1])) * signed_nine(terms[2]) + 128) >> 8) + expand(terms[3]);
    }
    return result;
}

unsigned final_alpha(s32 value, u64 modes, unsigned coverage, unsigned dither) {
    unsigned alpha = static_cast<unsigned>(clamp_color(value));
    alpha += (alpha + 1U) >> 8U;
    if ((modes & (1ULL << 13U)) != 0)
        alpha = (modes & (1ULL << 12U)) != 0 ? (alpha * coverage + 4U) >> 3U : coverage << 5U;
    else
        alpha += dither;
    return std::min(alpha, 255U);
}

} // namespace

RdpColor rdp_unpack_color(u32 value) {
    return {static_cast<s32>(value >> 24U), static_cast<s32>((value >> 16U) & 255U),
            static_cast<s32>((value >> 8U) & 255U), static_cast<s32>(value & 255U)};
}

unsigned rdp_combiner_texture_inputs(u64 combine, bool two_cycles) {
    constexpr unsigned shifts[2][8] = {{52, 28, 47, 15, 44, 12, 41, 9}, {37, 24, 32, 6, 21, 3, 18, 0}};
    constexpr unsigned masks[8] = {15, 15, 31, 7, 7, 7, 7, 7};
    unsigned result = 0;
    for (unsigned cycle = two_cycles ? 0U : 1U; cycle < 2; ++cycle) {
        for (unsigned term = 0; term < 8; ++term) {
            const unsigned selector = static_cast<unsigned>(combine >> shifts[cycle][term]) & masks[term];
            unsigned texel = selector;
            if (term == 2U && (selector == 8U || selector == 9U))
                texel -= 7U;
            if (texel == 1U || texel == 2U) {
                if (two_cycles && cycle == 1U)
                    texel = 3U - texel;
                result |= 1U << (texel - 1U);
            }
            if (two_cycles && ((term == 2U && selector == 13U) || (term == 6U && selector == 0U)))
                result |= 4U;
        }
    }
    return result;
}

RdpCombinedPixel rdp_combine(const RdpColorState& state, u64 modes, RdpColorInputs inputs, unsigned coverage,
                             unsigned alpha_dither) {
    RdpColor combined{};
    unsigned test_alpha = 0;
    const bool two_cycles = ((modes >> 52U) & 3U) == 1U;
    if (two_cycles) {
        combined = combine_cycle(state, inputs, combined, 0);
        test_alpha = final_alpha(combined[3], modes, coverage, alpha_dither);
        std::swap(inputs.texel0, inputs.texel1);
    }
    combined = combine_cycle(state, inputs, combined, 1);
    const unsigned alpha = final_alpha(combined[3], modes, coverage, alpha_dither);
    if ((modes & (1ULL << 12U)) != 0) {
        const unsigned clamped = static_cast<unsigned>(clamp_color(combined[3]));
        const unsigned expanded = clamped + ((clamped + 1U) >> 8U);
        coverage = ((expanded * coverage + 4U) >> 3U) >> 5U;
    }
    for (unsigned channel = 0; channel < 3; ++channel)
        combined[channel] = clamp_color(combined[channel]);
    combined[3] = static_cast<s32>(alpha);
    return {combined, coverage, two_cycles ? test_alpha : alpha};
}

} // namespace cupid
