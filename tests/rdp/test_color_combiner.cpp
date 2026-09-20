#include "color_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {
RdpCombinedPixel combine(CombineCycle cycle, RdpColorState state = {}, RdpColorInputs inputs = {}) {
    state.combine = combine_word({}, cycle);
    return rdp_combine(state, 0, inputs, 8, 0);
}
} // namespace

TEST(rdp_combiner_one_cycle_uses_second_mux) {
    RdpColorState state;
    state.primitive = 0x12345678;
    state.environment = 0xabcdef11;
    state.combine = combine_word({}, {.d = 5, .ad = 5});
    CHECK_EQ(rdp_combine(state, 0, {}, 8, 0).color, rdp_unpack_color(0xabcdef11));
}

TEST(rdp_combiner_add_inputs_keep_rgba_channels) {
    RdpColorState state;
    state.primitive = 0x12345678;
    state.environment = 0xabcdef11;
    RdpColorInputs inputs;
    inputs.texel0 = {11, 22, 33, 44};
    inputs.texel1 = {55, 66, 77, 88};
    inputs.shade = {99, 110, 121, 132};
    const RdpColor expected[] = {{},
                                 inputs.texel0,
                                 inputs.texel1,
                                 rdp_unpack_color(state.primitive),
                                 inputs.shade,
                                 rdp_unpack_color(state.environment),
                                 {255, 255, 255, 255},
                                 {}};
    for (unsigned selector = 0; selector < 8; ++selector)
        CHECK_EQ(combine({.d = selector, .ad = selector}, state, inputs).color, expected[selector]);
}

TEST(rdp_combiner_multiplies_with_eight_fraction_bits_and_rounding) {
    RdpColorState state;
    state.primitive = 0x03050780;
    state.environment = 0x80808080;
    CHECK_EQ(combine({.a = 3, .c = 5, .d = 7, .aa = 3, .ac = 5, .ad = 7}, state).color,
             (RdpColor{2, 3, 4, 64}));
}

TEST(rdp_combiner_rgb_alpha_multiplier_selectors) {
    RdpColorState state;
    state.primitive = 0xffffff20;
    state.environment = 0x01020330;
    state.primitive_lod = 64;
    RdpColorInputs inputs;
    inputs.texel0[3] = 80;
    inputs.texel1[3] = 96;
    inputs.shade[3] = 112;
    inputs.lod_fraction = 128;
    const s32 expected[] = {0, 80, 96, 32, 112, 48, 128, 64};
    for (unsigned selector = 7; selector <= 14; ++selector)
        CHECK_EQ(combine({.a = 6, .c = selector, .d = 7}, state, inputs).color[0], expected[selector - 7]);
}

TEST(rdp_combiner_key_center_scale_and_signed_convert_constants) {
    RdpColorState state;
    state.primitive = 0xc0a080ff;
    state.key_center = {64, 32, 0};
    state.key_scale = {128, 64, 32};
    CHECK_EQ(combine({.a = 3, .b = 6, .c = 6, .d = 7}, state).color, (RdpColor{64, 32, 16, 255}));
    state.convert[4] = 511;
    state.convert[5] = 128;
    CHECK_EQ(combine({.a = 8, .b = 7, .c = 15, .d = 7}, state).color[0], 1);
    state.convert[5] = 384;
    CHECK_EQ(combine({.a = 8, .b = 7, .c = 15, .d = 7}, state).color[0], 0);
}

TEST(rdp_combiner_special_nine_bit_overflow_clamps) {
    RdpColorState state;
    state.primitive = 0xffffffff;
    state.convert[5] = 256;
    CHECK_EQ(combine({.a = 3, .c = 15, .d = 7}, state).color[0], 255);
    state.convert[5] = 384;
    CHECK_EQ(combine({.a = 3, .c = 15, .d = 7}, state).color[0], 0);
}

TEST(rdp_combiner_two_cycles_retain_signed_intermediate) {
    RdpColorState state;
    state.primitive = 0xc8c8c8c8;
    state.environment = 0x64646464;
    state.primitive_lod = 128;
    state.combine = combine_word({.a = 5, .b = 3, .c = 14, .d = 7}, {.a = 8, .b = 0, .c = 14, .d = 7});
    CHECK_EQ(rdp_combine(state, 1ULL << 52U, {}, 8, 0).color[0], 25);
}

TEST(rdp_combiner_two_cycles_swap_texture_inputs) {
    RdpColorState state;
    RdpColorInputs inputs;
    inputs.texel0 = {10, 20, 30, 40};
    inputs.texel1 = {50, 60, 70, 80};
    state.combine = combine_word({.d = 1, .ad = 1}, {.d = 1, .ad = 1});
    auto pixel = rdp_combine(state, 1ULL << 52U, inputs, 8, 0);
    CHECK_EQ(pixel.color, inputs.texel1);
    CHECK_EQ(pixel.test_alpha, 40U);
}

TEST(rdp_combiner_alpha_coverage_modulation) {
    RdpColorState state;
    state.primitive = 0x12345680;
    state.combine = combine_word({}, {});
    const auto pixel = rdp_combine(state, (1ULL << 12U) | (1ULL << 13U), {}, 6, 7);
    CHECK_EQ(pixel.coverage, 3U);
    CHECK_EQ(pixel.color[3], 96);
    CHECK_EQ(pixel.test_alpha, 96U);
}

TEST(rdp_combiner_coverage_alpha_expands_full_opacity) {
    RdpColorState state;
    state.primitive = 0xffffffff;
    state.combine = combine_word({}, {});
    auto pixel = rdp_combine(state, (1ULL << 12U) | (1ULL << 13U), {}, 8, 0);
    CHECK_EQ(pixel.coverage, 8U);
    CHECK_EQ(pixel.color[3], 255);
    pixel = rdp_combine(state, 1ULL << 13U, {}, 3, 7);
    CHECK_EQ(pixel.color[3], 96);
}

TEST(rdp_combiner_two_cycle_alpha_test_uses_first_cycle) {
    RdpColorState state;
    state.primitive = 0xffffff80;
    state.environment = 0xffffffff;
    state.combine = combine_word({}, {.d = 5, .ad = 5});
    const auto pixel = rdp_combine(state, (1ULL << 52U) | (1ULL << 12U) | (1ULL << 13U), {}, 4, 0);
    CHECK_EQ(pixel.test_alpha, 64U);
    CHECK_EQ(pixel.color[3], 128);
    CHECK_EQ(pixel.coverage, 4U);
}

TEST(rdp_combiner_noise_input_and_unused_mux_codes) {
    RdpColorState state;
    state.primitive_lod = 128;
    RdpColorInputs inputs;
    inputs.noise = {97, 97};
    CHECK_EQ(combine({.a = 7, .c = 14, .d = 7}, state, inputs).color[0], 49);
    CHECK_EQ(combine({.a = 15, .b = 15, .c = 31, .d = 7}, state, inputs).color[0], 0);
}

TEST(rdp_blender_divider_wraps_and_uses_truncated_remainders) {
    CHECK_EQ(rdp_blend_divide(128, 8), 16U);
    CHECK_EQ(rdp_blend_divide(256, 1), 0U);
    CHECK_EQ(rdp_blend_divide(1280, 5), 51U);
    CHECK_EQ(rdp_blend_divide(0, 9), 227U);
    CHECK_EQ(rdp_blend_divide(2, 9), 226U);
    CHECK_EQ(rdp_blend_divide(0, 15), 170U);
}

TEST(rdp_blender_divider_complete_table_checksum) {
    u32 checksum = 2166136261U;
    for (unsigned denominator = 0; denominator < 16; ++denominator) {
        for (unsigned numerator = 0; numerator < 2048; ++numerator) {
            checksum ^= rdp_blend_divide(numerator, denominator);
            checksum *= 16777619U;
        }
    }
    CHECK_EQ(checksum, 0xc76cedc6U);
}

TEST(rdp_combiner_alpha_multiplier_mux_includes_both_lod_fractions) {
    RdpColorState state;
    state.primitive = 0x00000040;
    state.environment = 0x00000060;
    state.primitive_lod = 80;
    RdpColorInputs inputs;
    inputs.texel0[3] = 16;
    inputs.texel1[3] = 32;
    inputs.shade[3] = 48;
    inputs.lod_fraction = 112;
    const s32 expected[] = {112, 16, 32, 64, 48, 96, 80, 0};
    for (unsigned selector = 0; selector < 8; ++selector)
        CHECK_EQ(combine({.aa = 6, .ac = selector, .ad = 7}, state, inputs).color[3], expected[selector]);
}

TEST(rdp_blender_force_blend_uses_five_bit_factors) {
    const auto color = rdp_blend({}, (1ULL << 14U) | (1ULL << 22U), {200, 100, 50, 128}, {40, 60, 70, 224}, 0,
                                 true, true, 4);
    CHECK_EQ(color, (RdpColor{120, 80, 60, 128}));
}

TEST(rdp_blender_opaque_alpha_bypasses_blend_even_when_forced) {
    CHECK_EQ(rdp_blend({}, (1ULL << 14U) | (1ULL << 22U), {200, 100, 50, 255}, {40, 60, 70, 224}, 0, true,
                       true, 4),
             (RdpColor{200, 100, 50, 255}));
}

TEST(rdp_blender_color_on_coverage_selects_second_color) {
    RdpColorState state;
    state.fog = 0x11223344;
    CHECK_EQ(rdp_blend(state, (1ULL << 7U) | (3ULL << 22U), {1, 2, 3, 4}, {}, 0, true, false, 4),
             (RdpColor{17, 34, 51, 4}));
}

TEST(rdp_blender_memory_alpha_shifter_changes_weights) {
    const u64 modes = (1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U);
    CHECK_EQ(rdp_blend({}, modes, {160, 160, 160, 128}, {64, 64, 64, 224}, 0, true, true, 4)[0], 88);
    CHECK_EQ(rdp_blend({}, modes, {160, 160, 160, 128}, {64, 64, 64, 224}, 0, true, true, 0)[0], 144);
}

TEST(rdp_blender_two_cycles_feed_blended_rgb_to_second_cycle) {
    RdpColorState state;
    state.fog = 0x00000080;
    state.blend = 0x202020ff;
    const u64 modes = (1ULL << 52U) | (1ULL << 14U) | (2ULL << 22U) | (1ULL << 24U) | (1ULL << 20U);
    CHECK_EQ(rdp_blend(state, modes, {160, 96, 32, 128}, {64, 64, 64, 224}, 0, true, true, 4),
             (RdpColor{80, 64, 48, 128}));
}
