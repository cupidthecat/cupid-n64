#include "cupid/rdp/color_pipeline.hpp"

#include <algorithm>

namespace cupid {
namespace {

s32 signed_nine(s32 value) {
    return static_cast<s32>((static_cast<u32>(value) & 511U) ^ 256U) - 256;
}

s32 signed_seventeen(s32 value) {
    return static_cast<s32>((static_cast<u32>(value) & 0x1ffffU) ^ 0x10000U) - 0x10000;
}

s32 expand(s32 value) {
    return signed_nine(value - 128) + 128;
}

s32 clamp_color(s32 value) {
    return std::clamp(expand(value), 0, 255);
}

struct CycleResult {
    RdpColor color{};
    std::array<s32, 3> key_value{};
    std::array<s32, 3> bypass{};
};

using ResolvedTerms = std::array<RdpColor, 4>;

RdpColor unpack_color(u32 value) {
    return {static_cast<s32>(value >> 24U), static_cast<s32>((value >> 16U) & 255U),
            static_cast<s32>((value >> 8U) & 255U), static_cast<s32>(value & 255U)};
}

void fill_rgb(RdpColor& color, s32 value) {
    color[0] = color[1] = color[2] = value;
}

void copy_rgb(RdpColor& destination, const RdpColor& source) {
    for (unsigned channel = 0; channel < 3; ++channel)
        destination[channel] = source[channel];
}

void set_rgb_constant(RdpCombinerTermPlan& term, const RdpColor& color) {
    copy_rgb(term.constant, color);
}

void set_rgb_constant(RdpCombinerTermPlan& term, s32 value) {
    fill_rgb(term.constant, value);
}

void prepare_rgb_term(RdpCombinerTermPlan& term, const RdpColorState& state, unsigned equation_term,
                      unsigned selector, const RdpColor& primitive, const RdpColor& environment) {
    const auto common = [&] {
        switch (selector) {
        case 0:
            term.rgb = RdpCombinerSource::CombinedRgb;
            return true;
        case 1:
            term.rgb = RdpCombinerSource::Texel0Rgb;
            return true;
        case 2:
            term.rgb = RdpCombinerSource::Texel1Rgb;
            return true;
        case 3:
            set_rgb_constant(term, primitive);
            return true;
        case 4:
            term.rgb = RdpCombinerSource::ShadeRgb;
            return true;
        case 5:
            set_rgb_constant(term, environment);
            return true;
        default:
            return false;
        }
    };
    if (common())
        return;

    if (equation_term == 0U) {
        if (selector == 6U)
            set_rgb_constant(term, 256);
        else if (selector == 7U)
            term.rgb = RdpCombinerSource::Noise;
        return;
    }
    if (equation_term == 1U) {
        if (selector == 6U) {
            for (unsigned channel = 0; channel < 3; ++channel)
                term.constant[channel] = state.key_center[channel];
        } else if (selector == 7U) {
            set_rgb_constant(term, state.convert[4]);
        }
        return;
    }
    if (equation_term == 3U) {
        if (selector == 6U)
            set_rgb_constant(term, 256);
        return;
    }

    switch (selector) {
    case 6:
        for (unsigned channel = 0; channel < 3; ++channel)
            term.constant[channel] = state.key_scale[channel];
        break;
    case 7:
        term.rgb = RdpCombinerSource::CombinedAlpha;
        break;
    case 8:
        term.rgb = RdpCombinerSource::Texel0Alpha;
        break;
    case 9:
        term.rgb = RdpCombinerSource::Texel1Alpha;
        break;
    case 10:
        set_rgb_constant(term, primitive[3]);
        break;
    case 11:
        term.rgb = RdpCombinerSource::ShadeAlpha;
        break;
    case 12:
        set_rgb_constant(term, environment[3]);
        break;
    case 13:
        term.rgb = RdpCombinerSource::LodFraction;
        break;
    case 14:
        set_rgb_constant(term, state.primitive_lod);
        break;
    case 15:
        set_rgb_constant(term, state.convert[5]);
        break;
    default:
        break;
    }
}

void prepare_alpha_term(RdpCombinerTermPlan& term, const RdpColorState& state, unsigned equation_term,
                        unsigned selector, const RdpColor& primitive, const RdpColor& environment) {
    if (equation_term == 2U) {
        switch (selector) {
        case 0:
            term.alpha = RdpCombinerSource::LodFraction;
            break;
        case 1:
            term.alpha = RdpCombinerSource::Texel0Alpha;
            break;
        case 2:
            term.alpha = RdpCombinerSource::Texel1Alpha;
            break;
        case 3:
            term.constant[3] = primitive[3];
            break;
        case 4:
            term.alpha = RdpCombinerSource::ShadeAlpha;
            break;
        case 5:
            term.constant[3] = environment[3];
            break;
        case 6:
            term.constant[3] = state.primitive_lod;
            break;
        default:
            break;
        }
        return;
    }

    switch (selector) {
    case 0:
        term.alpha = RdpCombinerSource::CombinedAlpha;
        break;
    case 1:
        term.alpha = RdpCombinerSource::Texel0Alpha;
        break;
    case 2:
        term.alpha = RdpCombinerSource::Texel1Alpha;
        break;
    case 3:
        term.constant[3] = primitive[3];
        break;
    case 4:
        term.alpha = RdpCombinerSource::ShadeAlpha;
        break;
    case 5:
        term.constant[3] = environment[3];
        break;
    case 6:
        term.constant[3] = 256;
        break;
    default:
        break;
    }
}

ResolvedTerms resolve_raw_terms(const RdpColorState& state, const RdpColorInputs& inputs,
                                const RdpColor& combined, unsigned cycle) {
    constexpr unsigned rgb_shifts[2][4] = {{52, 28, 47, 15}, {37, 24, 32, 6}};
    constexpr unsigned alpha_shifts[2][4] = {{44, 12, 41, 9}, {21, 3, 18, 0}};
    constexpr unsigned rgb_masks[4] = {15, 15, 31, 7};
    const std::array<RdpColor, 6> colors = {combined,      inputs.texel0,
                                            inputs.texel1, unpack_color(state.primitive),
                                            inputs.shade,  unpack_color(state.environment)};
    ResolvedTerms terms{};
    for (unsigned channel = 0; channel < 4; ++channel) {
        for (unsigned term = 0; term < 4; ++term) {
            if (channel == 3U) {
                const unsigned selector =
                    static_cast<unsigned>(state.combine >> alpha_shifts[cycle][term]) & 7U;
                if (term == 2U) {
                    if (selector == 0U)
                        terms[term][channel] = inputs.lod_fraction;
                    else if (selector < 6U)
                        terms[term][channel] = colors[selector][3];
                    else if (selector == 6U)
                        terms[term][channel] = state.primitive_lod;
                } else if (selector < 6U)
                    terms[term][channel] = colors[selector][3];
                else if (selector == 6U)
                    terms[term][channel] = 256;
            } else {
                const unsigned selector =
                    static_cast<unsigned>(state.combine >> rgb_shifts[cycle][term]) & rgb_masks[term];
                if (selector < 6U)
                    terms[term][channel] = colors[selector][channel];
                else if (term == 2U) {
                    if (selector == 6U)
                        terms[term][channel] = state.key_scale[channel];
                    else if (selector <= 12U)
                        terms[term][channel] = colors[selector - 7U][3];
                    else if (selector == 13U)
                        terms[term][channel] = inputs.lod_fraction;
                    else if (selector == 14U)
                        terms[term][channel] = state.primitive_lod;
                    else if (selector == 15U)
                        terms[term][channel] = state.convert[5];
                } else if (term == 1U) {
                    if (selector == 6U)
                        terms[term][channel] = state.key_center[channel];
                    else if (selector == 7U)
                        terms[term][channel] = state.convert[4];
                } else if (selector == 6U)
                    terms[term][channel] = 256;
                else if (term == 0U && selector == 7U)
                    terms[term][channel] = inputs.noise[cycle];
            }
        }
    }
    return terms;
}

void resolve_rgb(RdpColor& value, RdpCombinerSource source, const RdpColorInputs& inputs,
                 const RdpColor& combined, unsigned cycle) {
    switch (source) {
    case RdpCombinerSource::CombinedRgb:
        copy_rgb(value, combined);
        break;
    case RdpCombinerSource::Texel0Rgb:
        copy_rgb(value, inputs.texel0);
        break;
    case RdpCombinerSource::Texel1Rgb:
        copy_rgb(value, inputs.texel1);
        break;
    case RdpCombinerSource::ShadeRgb:
        copy_rgb(value, inputs.shade);
        break;
    case RdpCombinerSource::CombinedAlpha:
        fill_rgb(value, combined[3]);
        break;
    case RdpCombinerSource::Texel0Alpha:
        fill_rgb(value, inputs.texel0[3]);
        break;
    case RdpCombinerSource::Texel1Alpha:
        fill_rgb(value, inputs.texel1[3]);
        break;
    case RdpCombinerSource::ShadeAlpha:
        fill_rgb(value, inputs.shade[3]);
        break;
    case RdpCombinerSource::LodFraction:
        fill_rgb(value, inputs.lod_fraction);
        break;
    case RdpCombinerSource::Noise:
        fill_rgb(value, inputs.noise[cycle]);
        break;
    default:
        break;
    }
}

void resolve_alpha(RdpColor& value, RdpCombinerSource source, const RdpColorInputs& inputs,
                   const RdpColor& combined) {
    switch (source) {
    case RdpCombinerSource::CombinedAlpha:
        value[3] = combined[3];
        break;
    case RdpCombinerSource::Texel0Alpha:
        value[3] = inputs.texel0[3];
        break;
    case RdpCombinerSource::Texel1Alpha:
        value[3] = inputs.texel1[3];
        break;
    case RdpCombinerSource::ShadeAlpha:
        value[3] = inputs.shade[3];
        break;
    case RdpCombinerSource::LodFraction:
        value[3] = inputs.lod_fraction;
        break;
    default:
        break;
    }
}

bool same_rgb_term(const RdpCombinerTermPlan& first, const RdpCombinerTermPlan& second) {
    if (first.rgb != second.rgb)
        return false;
    if (first.rgb != RdpCombinerSource::Constant)
        return true;
    for (unsigned channel = 0; channel < 3; ++channel)
        if (expand(first.constant[channel]) != expand(second.constant[channel]))
            return false;
    return true;
}

bool same_alpha_term(const RdpCombinerTermPlan& first, const RdpCombinerTermPlan& second) {
    if (first.alpha != second.alpha)
        return false;
    return first.alpha != RdpCombinerSource::Constant ||
           expand(first.constant[3]) == expand(second.constant[3]);
}

bool zero_rgb_multiplier(const RdpCombinerTermPlan& term) {
    if (term.rgb != RdpCombinerSource::Constant)
        return false;
    for (unsigned channel = 0; channel < 3; ++channel)
        if (signed_nine(term.constant[channel]) != 0)
            return false;
    return true;
}

bool zero_alpha_multiplier(const RdpCombinerTermPlan& term) {
    return term.alpha == RdpCombinerSource::Constant && signed_nine(term.constant[3]) == 0;
}

RdpColor resolve_prepared_rgb(const RdpCombinerTermPlan& term, const RdpColorInputs& inputs,
                              const RdpColor& combined, unsigned cycle) {
    RdpColor value = term.constant;
    resolve_rgb(value, term.rgb, inputs, combined, cycle);
    return value;
}

s32 resolve_prepared_alpha(const RdpCombinerTermPlan& term, const RdpColorInputs& inputs,
                           const RdpColor& combined) {
    RdpColor value = term.constant;
    resolve_alpha(value, term.alpha, inputs, combined);
    return value[3];
}

void evaluate_prepared_rgb(CycleResult& result, const RdpCombinerPlan& plan, const RdpColorInputs& inputs,
                           const RdpColor& combined, unsigned cycle, bool key_enabled) {
    const auto& terms = plan.cycles[cycle];
    const auto d = resolve_prepared_rgb(terms[3], inputs, combined, cycle);
    if (plan.rgb_expression[cycle] == RdpCombinerExpression::DirectD) {
        for (unsigned channel = 0; channel < 3; ++channel)
            result.color[channel] = expand(d[channel]);
        if (!key_enabled)
            return;
        const auto a = resolve_prepared_rgb(terms[0], inputs, combined, cycle);
        for (unsigned channel = 0; channel < 3; ++channel) {
            result.key_value[channel] = signed_seventeen(128 + result.color[channel] * 256);
            result.bypass[channel] = a[channel];
        }
        return;
    }

    const auto a = resolve_prepared_rgb(terms[0], inputs, combined, cycle);
    const auto b = resolve_prepared_rgb(terms[1], inputs, combined, cycle);
    const auto c = resolve_prepared_rgb(terms[2], inputs, combined, cycle);
    for (unsigned channel = 0; channel < 3; ++channel) {
        const s32 expanded_d = expand(d[channel]);
        const s32 multiplied = (expand(a[channel]) - expand(b[channel])) * signed_nine(c[channel]) + 128;
        result.color[channel] = (multiplied >> 8) + expanded_d;
        if (key_enabled) {
            result.key_value[channel] = signed_seventeen(multiplied + expanded_d * 256);
            result.bypass[channel] = a[channel];
        }
    }
}

void evaluate_prepared_alpha(CycleResult& result, const RdpCombinerPlan& plan, const RdpColorInputs& inputs,
                             const RdpColor& combined, unsigned cycle) {
    const auto& terms = plan.cycles[cycle];
    const s32 d = expand(resolve_prepared_alpha(terms[3], inputs, combined));
    if (plan.alpha_expression[cycle] == RdpCombinerExpression::DirectD) {
        result.color[3] = d;
        return;
    }
    const s32 a = expand(resolve_prepared_alpha(terms[0], inputs, combined));
    const s32 b = expand(resolve_prepared_alpha(terms[1], inputs, combined));
    const s32 c = signed_nine(resolve_prepared_alpha(terms[2], inputs, combined));
    result.color[3] = (((a - b) * c + 128) >> 8) + d;
}

CycleResult evaluate_prepared_cycle(const RdpCombinerPlan& plan, const RdpColorInputs& inputs,
                                    const RdpColor& combined, unsigned cycle, bool key_enabled) {
    CycleResult result;
    evaluate_prepared_rgb(result, plan, inputs, combined, cycle, key_enabled);
    evaluate_prepared_alpha(result, plan, inputs, combined, cycle);
    return result;
}

CycleResult evaluate_cycle(const ResolvedTerms& terms) {
    CycleResult result;
    for (unsigned channel = 0; channel < 4; ++channel) {
        const s32 multiplied =
            (expand(terms[0][channel]) - expand(terms[1][channel])) * signed_nine(terms[2][channel]) + 128;
        result.color[channel] = (multiplied >> 8) + expand(terms[3][channel]);
        if (channel < 3U) {
            result.key_value[channel] = signed_seventeen(multiplied + (expand(terms[3][channel]) << 8));
            result.bypass[channel] = terms[0][channel];
        }
    }
    return result;
}

unsigned key_alpha(const RdpColorState& state, const std::array<s32, 3>& values) {
    s32 alpha = 255;
    for (unsigned channel = 0; channel < 3; ++channel) {
        const s32 magnitude = values[channel] < 0 ? -values[channel] : values[channel];
        const s32 channel_alpha = (static_cast<s32>(state.key_width[channel]) << 4) - magnitude;
        alpha = std::min(alpha, channel_alpha);
    }
    return static_cast<unsigned>(std::clamp(alpha, 0, 255));
}

unsigned final_alpha(s32 value, u64 modes, unsigned coverage, unsigned dither, bool key_enabled = false,
                     unsigned key = 0) {
    unsigned alpha = static_cast<unsigned>(clamp_color(value));
    alpha += (alpha + 1U) >> 8U;
    if ((modes & (1ULL << 13U)) != 0) {
        alpha = (modes & (1ULL << 12U)) != 0 ? (alpha * coverage + 4U) >> 3U : coverage << 5U;
    } else if (key_enabled) {
        alpha = key;
    } else {
        alpha += dither;
    }
    return std::min(alpha, 255U);
}

template <typename Cycle>
RdpCombinedPixel combine_impl(const RdpColorState& state, u64 modes, const RdpColorInputs& inputs,
                              unsigned coverage, unsigned alpha_dither, Cycle&& cycle) {
    RdpColor combined{};
    unsigned test_alpha = 0;
    const bool two_cycles = ((modes >> 52U) & 3U) == 1U;
    const bool key_enabled = (modes & (1ULL << 40U)) != 0;
    CycleResult result;
    if (two_cycles) {
        result = cycle(inputs, combined, 0);
        combined = result.color;
        const unsigned key = key_enabled ? key_alpha(state, result.key_value) : 0;
        test_alpha = final_alpha(combined[3], modes, coverage, alpha_dither, key_enabled, key);
        RdpColorInputs cycle1_inputs = inputs;
        std::swap(cycle1_inputs.texel0, cycle1_inputs.texel1);
        result = cycle(cycle1_inputs, combined, 1);
    } else {
        result = cycle(inputs, combined, 1);
    }
    combined = result.color;
    const unsigned key = key_enabled ? key_alpha(state, result.key_value) : 0;
    const unsigned alpha = final_alpha(combined[3], modes, coverage, alpha_dither, key_enabled, key);
    if ((modes & (1ULL << 12U)) != 0) {
        const unsigned clamped = static_cast<unsigned>(clamp_color(combined[3]));
        const unsigned expanded = clamped + ((clamped + 1U) >> 8U);
        coverage = ((expanded * coverage + 4U) >> 3U) >> 5U;
    }
    for (unsigned channel = 0; channel < 3; ++channel)
        combined[channel] = clamp_color(key_enabled ? result.bypass[channel] : combined[channel]);
    combined[3] = static_cast<s32>(alpha);
    return {combined, coverage, two_cycles ? test_alpha : alpha};
}

} // namespace

RdpColor rdp_unpack_color(u32 value) {
    return unpack_color(value);
}

RdpCombinerPlan rdp_prepare_combiner(const RdpColorState& state) {
    constexpr unsigned rgb_shifts[2][4] = {{52, 28, 47, 15}, {37, 24, 32, 6}};
    constexpr unsigned alpha_shifts[2][4] = {{44, 12, 41, 9}, {21, 3, 18, 0}};
    constexpr unsigned rgb_masks[4] = {15, 15, 31, 7};
    const RdpColor primitive = unpack_color(state.primitive);
    const RdpColor environment = unpack_color(state.environment);
    RdpCombinerPlan plan;
    for (unsigned cycle = 0; cycle < plan.cycles.size(); ++cycle) {
        for (unsigned term = 0; term < plan.cycles[cycle].size(); ++term) {
            auto& prepared = plan.cycles[cycle][term];
            const unsigned rgb =
                static_cast<unsigned>(state.combine >> rgb_shifts[cycle][term]) & rgb_masks[term];
            const unsigned alpha = static_cast<unsigned>(state.combine >> alpha_shifts[cycle][term]) & 7U;
            prepare_rgb_term(prepared, state, term, rgb, primitive, environment);
            prepare_alpha_term(prepared, state, term, alpha, primitive, environment);
        }
        plan.rgb_expression[cycle] = zero_rgb_multiplier(plan.cycles[cycle][2]) ||
                                             same_rgb_term(plan.cycles[cycle][0], plan.cycles[cycle][1])
                                         ? RdpCombinerExpression::DirectD
                                         : RdpCombinerExpression::Full;
        plan.alpha_expression[cycle] = zero_alpha_multiplier(plan.cycles[cycle][2]) ||
                                               same_alpha_term(plan.cycles[cycle][0], plan.cycles[cycle][1])
                                           ? RdpCombinerExpression::DirectD
                                           : RdpCombinerExpression::Full;
        plan.uses_noise[cycle] = plan.cycles[cycle][0].rgb == RdpCombinerSource::Noise;
    }
    return plan;
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

RdpCombinedPixel rdp_combine(const RdpColorState& state, u64 modes, const RdpColorInputs& inputs,
                             unsigned coverage, unsigned alpha_dither) {
    return combine_impl(state, modes, inputs, coverage, alpha_dither,
                        [&](const RdpColorInputs& cycle_inputs, const RdpColor& combined, unsigned cycle) {
                            return evaluate_cycle(resolve_raw_terms(state, cycle_inputs, combined, cycle));
                        });
}

RdpCombinedPixel rdp_combine_prepared(const RdpColorState& state, const RdpCombinerPlan& plan, u64 modes,
                                      const RdpColorInputs& inputs, unsigned coverage,
                                      unsigned alpha_dither) {
    const bool key_enabled = (modes & (1ULL << 40U)) != 0;
    return combine_impl(state, modes, inputs, coverage, alpha_dither,
                        [&](const RdpColorInputs& cycle_inputs, const RdpColor& combined, unsigned cycle) {
                            return evaluate_prepared_cycle(plan, cycle_inputs, combined, cycle, key_enabled);
                        });
}

} // namespace cupid
