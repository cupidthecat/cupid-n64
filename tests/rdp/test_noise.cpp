#include "color_commands.hpp"
#include "cupid/rdp/noise.hpp"

using namespace cupid;
using namespace test::rdp;

TEST(rdp_noise_combiner_has_three_random_bits_and_fixed_low_bits) {
    constexpr s32 expected[] = {32, 96, 160, 224, 288, 352, 416, 480};
    for (unsigned sample = 0; sample < 65536; ++sample)
        CHECK_EQ(rdp_combiner_noise(static_cast<u16>(sample)), expected[sample & 7U]);
}

TEST(rdp_noise_dither_splits_rgb_bits_and_shares_red_with_alpha) {
    const u64 modes = (2ULL << 38U) | (2ULL << 36U);
    CHECK_EQ(rdp_dither_coefficients(modes, 0, 0, 0x1a3), (std::array<unsigned, 4>{3, 4, 6, 3}));
    CHECK_EQ(rdp_dither_coefficients(modes, 0, 0, 0x038), (std::array<unsigned, 4>{0, 7, 0, 0}));
    CHECK_EQ(rdp_dither_coefficients(modes, 0, 0, 0xffff), (std::array<unsigned, 4>{7, 7, 7, 7}));
    CHECK_EQ(rdp_dither_coefficients(modes, 0, 0, 0xfe00), (std::array<unsigned, 4>{0, 0, 0, 0}));
}

TEST(rdp_dither_selectors_preserve_matrix_alpha_with_random_or_disabled_rgb) {
    constexpr unsigned alpha_thresholds[] = {6, 4, 6, 4};
    for (unsigned rgb = 0; rgb < 4; ++rgb) {
        for (unsigned alpha = 0; alpha < 4; ++alpha) {
            const auto result = rdp_dither_coefficients(
                (static_cast<u64>(rgb) << 38U) | (static_cast<u64>(alpha) << 36U), 1, 0, 0x1a3);
            const unsigned expected = alpha == 0U   ? alpha_thresholds[rgb]
                                      : alpha == 1U ? 7U - alpha_thresholds[rgb]
                                      : alpha == 2U ? 3U
                                                    : 0U;
            CHECK_EQ(result[3], expected);
            if (rgb < 2U)
                for (unsigned channel = 0; channel < 3; ++channel)
                    CHECK_EQ(result[channel], alpha_thresholds[rgb]);
            if (rgb == 3U)
                CHECK_EQ(result, (std::array<unsigned, 4>{7, 7, 7, expected}));
        }
    }
}

TEST(rdp_combiner_uses_separate_noise_samples_in_two_cycles) {
    RdpColorState state;
    state.primitive = 0xffffffff;
    state.primitive_lod = 128;
    state.combine = combine_word({.a = 7, .c = 14, .d = 7}, {.a = 7, .c = 14, .d = 0});
    RdpColorInputs inputs;
    inputs.noise = {32, 224};
    CHECK_EQ(rdp_combine(state, 1ULL << 52U, inputs, 8, 0).color, rdp_unpack_color(0x808080ff));
    CHECK_EQ(rdp_combine(state, 0, inputs, 8, 0).color, rdp_unpack_color(0x707070ff));
}

TEST(rdp_noise_sample_is_stable_at_unsigned_coordinate_boundaries) {
    const u16 first = rdp_pixel_noise(0xffffffff, 0xffffffff, 0xffffffff);
    CHECK_EQ(rdp_pixel_noise(0xffffffff, 0xffffffff, 0xffffffff), first);
    CHECK(rdp_pixel_noise(0, 0, 0) != first);
}
