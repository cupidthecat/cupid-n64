#include "cupid/rdp/texture_sampling.hpp"
#include "test.hpp"

using namespace cupid;

TEST(rdp_lod_magnification_selects_base_tile) {
    const auto lod = rdp_texture_lod({0, 0}, {16, 0}, {0, 16}, 2, 3, 0, 1ULL << 48U, false);
    CHECK_EQ(lod.first_tile, 2U);
    CHECK_EQ(lod.second_tile, 2U);
    CHECK_EQ(lod.fraction, 0);
}

TEST(rdp_lod_minification_selects_level_and_fraction) {
    auto lod = rdp_texture_lod({0, 0}, {48, 0}, {0, 32}, 2, 3, 0, 1ULL << 48U, false);
    CHECK_EQ(lod.first_tile, 2U);
    CHECK_EQ(lod.second_tile, 3U);
    CHECK_EQ(lod.fraction, 128);
    lod = rdp_texture_lod({0, 0}, {64, 0}, {0, 32}, 2, 3, 0, 1ULL << 48U, false);
    CHECK_EQ(lod.first_tile, 3U);
    CHECK_EQ(lod.second_tile, 4U);
    CHECK_EQ(lod.fraction, 0);
}

TEST(rdp_lod_sharpen_preserves_signed_magnification_fraction) {
    auto lod = rdp_texture_lod({0, 0}, {16, 0}, {0, 16}, 2, 3, 0, (1ULL << 48U) | (1ULL << 49U), false);
    CHECK_EQ(lod.first_tile, 2U);
    CHECK_EQ(lod.second_tile, 3U);
    CHECK_EQ(lod.fraction, -128);
    lod = rdp_texture_lod({0, 0}, {16, 0}, {0, 16}, 2, 3, 24, (1ULL << 48U) | (1ULL << 49U), false);
    CHECK_EQ(lod.fraction, -64);
}

TEST(rdp_lod_negative_derivatives_use_ones_complement_magnitude) {
    const auto lod = rdp_texture_lod({0, 0}, {-32, 0}, {0, 0}, 0, 3, 0, 1ULL << 49U, false);
    CHECK_EQ(lod.fraction, -8);
}

TEST(rdp_lod_distant_and_overflow_select_maximum_level) {
    auto lod = rdp_texture_lod({0, 0}, {256, 0}, {0, 0}, 2, 3, 0, 1ULL << 48U, false);
    CHECK_EQ(lod.first_tile, 5U);
    CHECK_EQ(lod.second_tile, 5U);
    CHECK_EQ(lod.fraction, 255);
    lod = rdp_texture_lod({0, 0}, {0, 0}, {0, 0}, 2, 3, 0, 1ULL << 48U, true);
    CHECK_EQ(lod.first_tile, 5U);
    CHECK_EQ(lod.second_tile, 5U);
    CHECK_EQ(lod.fraction, 255);
}

TEST(rdp_lod_detail_selects_adjacent_levels_and_wraps_tile_index) {
    auto lod = rdp_texture_lod({0, 0}, {64, 0}, {0, 32}, 7, 3, 0, (1ULL << 48U) | (1ULL << 50U), false);
    CHECK_EQ(lod.first_tile, 1U);
    CHECK_EQ(lod.second_tile, 2U);
    lod = rdp_texture_lod({0, 0}, {16, 0}, {0, 16}, 7, 3, 0, (1ULL << 48U) | (1ULL << 50U), false);
    CHECK_EQ(lod.first_tile, 7U);
    CHECK_EQ(lod.second_tile, 0U);
    CHECK_EQ(lod.fraction, 128);
}

TEST(rdp_lod_disabled_keeps_tiles_but_still_computes_fraction) {
    const auto lod = rdp_texture_lod({0, 0}, {96, 0}, {0, 32}, 7, 3, 0, 0, false);
    CHECK_EQ(lod.first_tile, 7U);
    CHECK_EQ(lod.second_tile, 0U);
    CHECK_EQ(lod.fraction, 128);
}

TEST(rdp_lod_rectangle_zero_maximum_level_and_detail) {
    auto lod = rdp_texture_lod({0, 0}, {32, 0}, {0, 32}, 2, 0, 0, 1ULL << 48U, false);
    CHECK_EQ(lod.first_tile, 2U);
    CHECK_EQ(lod.second_tile, 2U);
    CHECK_EQ(lod.fraction, 255);
    lod = rdp_texture_lod({0, 0}, {32, 0}, {0, 32}, 2, 0, 0, (1ULL << 48U) | (1ULL << 50U), false);
    CHECK_EQ(lod.first_tile, 3U);
    CHECK_EQ(lod.second_tile, 3U);
}
