#include "filter_fixture.hpp"
#include "test.hpp"

#include <algorithm>
#include <cmath>

using namespace test::vi;

TEST(vi_coverage_filter_weights_the_second_extrema_for_all_coverages) {
    for (u32 coverage = 0; coverage < 8; ++coverage) {
        FilterFixture fixture;
        fixture.gray(8, 3, 80, coverage);
        fixture.neighbors({10, 20, 120, 180, 200, 220});
        const u32 expected = 80 + ((60 * (7 - coverage) + 4) >> 3);
        CHECK_EQ(fixture.sample(), (ViColor{expected, expected, expected}));
    }
}

TEST(vi_coverage_filter_excludes_partial_neighbors) {
    FilterFixture fixture;
    fixture.gray(8, 3, 80, 0);
    fixture.neighbors({10, 20, 120, 180, 200, 220}, 6);
    CHECK_EQ(fixture.sample(), (ViColor{80, 80, 80}));
    fixture.gray(7, 2, 200);
    CHECK_EQ(fixture.sample(), (ViColor{80, 80, 80}));
    fixture.gray(9, 2, 200);
    CHECK_EQ(fixture.sample(), (ViColor{185, 185, 185}));
}

TEST(vi_coverage_filter_rounds_negative_corrections_arithmetically) {
    FilterFixture fixture;
    fixture.gray(8, 3, 200, 4);
    fixture.neighbors({0, 10, 20, 30, 40, 50});
    CHECK_EQ(fixture.sample(), (ViColor{129, 129, 129}));
}

TEST(vi_coverage_filter_combines_visible_and_hidden_rgba16_coverage) {
    for (u32 coverage = 0; coverage < 8; ++coverage) {
        FilterFixture fixture(2);
        fixture.gray(8, 3, 80, coverage);
        fixture.neighbors({8, 16, 120, 176, 200, 216});
        const u32 expected = 80 + ((56 * (7 - coverage) + 4) >> 3);
        CHECK_EQ(fixture.sample(), (ViColor{expected, expected, expected}));
    }
}

TEST(vi_resample_and_replicate_modes_force_full_coverage) {
    for (u32 mode = 0; mode < 4; ++mode) {
        FilterFixture fixture;
        fixture.registers[0] |= mode << 8;
        fixture.gray(8, 3, 80, 0);
        fixture.neighbors({10, 20, 120, 180, 200, 220});
        const u32 expected = mode < 2 ? 133 : 80;
        CHECK_EQ(fixture.sample(), (ViColor{expected, expected, expected}));
    }
}

TEST(vi_repeated_row_filter_substitutes_the_lower_neighbors) {
    FilterFixture fixture;
    fixture.gray(8, 3, 80, 0);
    fixture.neighbors({10, 20, 120, 180, 200, 220});
    CHECK_EQ(fixture.sample(), (ViColor{133, 133, 133}));
    CHECK_EQ(fixture.sample(8, 3, true), (ViColor{115, 115, 115}));
}

TEST(vi_dither_restoration_uses_clamped_five_bit_neighbor_differences) {
    FilterFixture fixture;
    fixture.registers[0] |= 0x10000;
    fixture.put(8, 3, {103, 160, 248});
    for (s32 y = 2; y <= 4; ++y)
        for (s32 x = 7; x <= 9; ++x)
            if (x != 8 || y != 3)
                fixture.put(x, y, {240, 0, 255}, 0);
    CHECK_EQ(fixture.sample(), (ViColor{104, 152, 248}));
    CHECK_EQ(fixture.sample(8, 3, true), (ViColor{103, 153, 248}));
}

TEST(vi_dither_restoration_is_skipped_for_partial_center_coverage) {
    FilterFixture fixture;
    fixture.registers[0] |= 0x10000;
    fixture.gray(8, 3, 103, 6);
    fixture.neighbors({240, 240, 240, 240, 240, 240}, 6);
    CHECK_EQ(fixture.sample(), (ViColor{103, 103, 103}));
    fixture.registers[0] |= 0x200;
    CHECK_EQ(fixture.sample(), (ViColor{96, 96, 96}));
}

TEST(vi_divot_takes_independent_channel_medians_when_any_coverage_is_partial) {
    FilterFixture fixture;
    fixture.registers[0] |= 16;
    fixture.put(7, 3, {10, 200, 30}, 0);
    fixture.put(8, 3, {100, 20, 250});
    fixture.put(9, 3, {80, 160, 40}, 0);
    CHECK_EQ(fixture.sample(), (ViColor{80, 160, 40}));
    fixture.put(7, 3, {10, 200, 30});
    fixture.put(9, 3, {80, 160, 40});
    CHECK_EQ(fixture.sample(), (ViColor{100, 20, 250}));
}

TEST(vi_divot_uses_reconstructed_neighbor_colors) {
    FilterFixture fixture;
    fixture.registers[0] |= 16;
    fixture.gray(7, 3, 10, 0);
    fixture.gray(8, 3, 250);
    fixture.gray(9, 3, 30, 0);
    fixture.gray(6, 2, 200);
    fixture.gray(6, 4, 200);
    fixture.gray(10, 2, 200);
    fixture.gray(10, 4, 200);
    CHECK_EQ(fixture.sample(), (ViColor{179, 179, 179}));
}

TEST(vi_filter_neighbor_fetches_wrap_before_the_framebuffer_origin) {
    for (unsigned format : {2U, 3U}) {
        FilterFixture fixture(format);
        fixture.gray(0, 0, 80, 0);
        fixture.gray(-1, -1, 200);
        fixture.gray(1, -1, 200);
        CHECK_EQ(fixture.sample(0, 0), (ViColor{185, 185, 185}));
    }
}

TEST(vi_gamma_matches_integer_square_root_quantization) {
    for (u32 value = 0; value < 256; ++value) {
        const u32 expected = static_cast<u32>(std::sqrt(static_cast<double>(value * 64))) * 2;
        CHECK_EQ(vi_gamma({value, value, value}, true, false, 65535),
                 (ViColor{expected, expected, expected}));
        CHECK_EQ(vi_gamma({value, value, value}, false, false, 65535), (ViColor{value, value, value}));
    }
}

TEST(vi_gamma_dither_addresses_all_table_entries) {
    for (u32 value = 0; value < 256; ++value)
        for (u32 noise = 0; noise < 64; ++noise) {
            const u32 expected = static_cast<u32>(std::sqrt(static_cast<double>(value * 64 + noise))) * 2;
            CHECK_EQ(vi_gamma({value, value, value}, true, true, static_cast<u16>(noise))[0], expected);
        }
}

TEST(vi_gamma_dither_preserves_channel_bit_correlations) {
    for (u32 noise = 0; noise < 65536; ++noise) {
        const u32 red = noise & 63U;
        const u32 green = (noise / 64) & 63U;
        const u32 blue = ((noise / 4096) & 7U) * 8 + (noise & 7U);
        const ViColor expected = {static_cast<u32>(std::sqrt(static_cast<double>(red))) * 2,
                                  static_cast<u32>(std::sqrt(static_cast<double>(64 * 64 + green))) * 2,
                                  static_cast<u32>(std::sqrt(static_cast<double>(255 * 64 + blue))) * 2};
        CHECK_EQ(vi_gamma({0, 64, 255}, true, true, static_cast<u16>(noise)), expected);
    }
}

TEST(vi_gamma_disabled_dither_adds_independent_low_bits_and_saturates) {
    for (u16 noise = 0; noise < 8; ++noise) {
        const ViColor expected = {10U + (noise & 1U), 254U + ((noise >> 1) & 1U), 255};
        CHECK_EQ(vi_gamma({10, 254, 255}, false, true, noise), expected);
    }
}
