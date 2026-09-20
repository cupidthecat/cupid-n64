#include "cupid/rdp/color_pipeline.hpp"
#include "cupid/rdp/depth.hpp"
#include "test.hpp"

#include <array>

using namespace cupid;

namespace {
constexpr u64 compare = 1ULL << 4U;
constexpr u64 aa = 1ULL << 3U;
constexpr u64 force = 1ULL << 14U;
} // namespace

TEST(rdp_depth_compression_exponent_boundaries) {
    constexpr u32 base[] = {0, 0x20000, 0x30000, 0x38000, 0x3c000, 0x3e000, 0x3f000, 0x3f800};
    constexpr unsigned step[] = {64, 32, 16, 8, 4, 2, 1, 1};
    for (unsigned exponent = 0; exponent < 8; ++exponent) {
        CHECK_EQ(rdp_compress_depth(base[exponent]), exponent * 0x800U);
        CHECK_EQ(rdp_decompress_depth(static_cast<u16>(exponent * 0x800U)), base[exponent]);
        CHECK_EQ(rdp_decompress_depth(static_cast<u16>(exponent * 0x800U + 0x7ffU)),
                 base[exponent] + 2047U * step[exponent]);
        if (exponent > 0)
            CHECK_EQ(rdp_compress_depth(base[exponent] - 1U), exponent * 0x800U - 1U);
    }
    CHECK_EQ(rdp_compress_depth(0x3ffff), 0x3fffU);
    CHECK_EQ(rdp_decompress_depth(0x3fff), 0x3ffffU);
}

TEST(rdp_depth_compression_all_values_round_down_monotonically) {
    unsigned previous = 0;
    for (u32 depth = 0; depth < 0x40000; ++depth) {
        const u16 encoded = rdp_compress_depth(depth);
        const u32 decoded = rdp_decompress_depth(encoded);
        CHECK(decoded <= depth);
        CHECK(depth - decoded < 64U);
        CHECK(encoded == previous || encoded == previous + 1U);
        CHECK_EQ(rdp_compress_depth(decoded), encoded);
        previous = encoded;
    }
    CHECK_EQ(previous, 0x3fffU);
}

TEST(rdp_depth_delta_encoding_all_bit_combinations) {
    for (unsigned value = 0; value < 65536; ++value) {
        unsigned expected = 0;
        for (unsigned bit = 0; bit < 16; ++bit) {
            if ((value & (1U << bit)) != 0)
                expected |= bit;
        }
        CHECK_EQ(rdp_compress_depth_delta(static_cast<u16>(value)), expected);
    }
    CHECK_EQ(rdp_compress_depth_delta(0x1800), 15U);
    CHECK_EQ(rdp_compress_depth_delta(0), 0U);
}

TEST(rdp_depth_without_comparison_sets_blend_from_coverage) {
    const auto partial = rdp_test_depth({0x20000, 1}, 0xffff, 3, 4, 3, aa);
    CHECK(partial.pass);
    CHECK(partial.blend_enabled);
    CHECK(!partial.coverage_wrap);
    CHECK_EQ(partial.coverage, 4U);
    CHECK_EQ(partial.pixel_alpha_shift, 0U);
    CHECK_EQ(partial.memory_alpha_shift, 4U);
    const auto full = rdp_test_depth({0x20000, 0x8000}, 0, 0, 4, 4, aa);
    CHECK(full.pass);
    CHECK(!full.blend_enabled);
    CHECK(full.coverage_wrap);
    CHECK_EQ(full.memory_alpha_shift, 0U);
    CHECK(rdp_test_depth({}, 0, 0, 8, 7, force).blend_enabled);
}

TEST(rdp_depth_opaque_overflow_requires_strictly_nearer_depth) {
    CHECK(rdp_test_depth({0x1ffff, 1}, 0x2000, 0, 8, 0, compare).pass);
    CHECK(!rdp_test_depth({0x20000, 1}, 0x2000, 0, 8, 0, compare).pass);
    CHECK(!rdp_test_depth({0x20001, 1}, 0x2000, 0, 8, 0, compare).pass);
}

TEST(rdp_depth_opaque_tolerance_depends_on_depth_precision) {
    constexpr u16 words[] = {0, 0x2000, 0x4000, 0x6000};
    constexpr u32 depths[] = {0, 0x20000, 0x30000, 0x38000};
    constexpr u32 tolerances[] = {128, 64, 32, 8};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(rdp_test_depth({depths[i] + tolerances[i], 1}, words[i], 0, 4, 0, compare).pass);
        CHECK(!rdp_test_depth({depths[i] + tolerances[i] + 1U, 1}, words[i], 0, 4, 0, compare).pass);
    }
}

TEST(rdp_depth_farther_test_controls_antialias_blending) {
    const auto distant = rdp_test_depth({0x1ffbf, 1}, 0x2000, 0, 4, 0, compare | aa);
    CHECK(distant.pass);
    CHECK(!distant.blend_enabled);
    const auto nearby = rdp_test_depth({0x1ffc0, 1}, 0x2000, 0, 4, 0, compare | aa);
    CHECK(nearby.pass);
    CHECK(nearby.blend_enabled);
    CHECK(rdp_test_depth({0x1ffbf, 1}, 0x2000, 0, 4, 0, compare | aa | force).blend_enabled);
}

TEST(rdp_depth_delta_combines_with_bitwise_or_before_rounding) {
    // Delta 12 combined with memory delta 8 uses tolerance 64, not 96 or 128.
    CHECK(rdp_test_depth({0x3c040, 12}, 0x8000, 3, 4, 0, compare).pass);
    CHECK(!rdp_test_depth({0x3c041, 12}, 0x8000, 3, 4, 0, compare).pass);
}

TEST(rdp_depth_maximum_stored_depth_passes_except_decal) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        const auto result =
            rdp_test_depth({0x3ffff, 0}, 0xfffc, 0, 8, 7, compare | (static_cast<u64>(mode) << 10U));
        CHECK_EQ(result.pass, mode != 3U);
    }
}

TEST(rdp_depth_transparent_mode_ignores_tolerance_and_coverage) {
    const u64 modes = compare | (2ULL << 10U);
    CHECK(rdp_test_depth({0x1ffff, 0xffff}, 0x2003, 3, 8, 7, modes).pass);
    CHECK(!rdp_test_depth({0x20000, 0xffff}, 0x2003, 3, 1, 0, modes).pass);
    CHECK(!rdp_test_depth({0x20001, 0xffff}, 0x2003, 3, 1, 0, modes).pass);
}

TEST(rdp_depth_decal_window_includes_both_boundaries) {
    const u64 modes = compare | (3ULL << 10U);
    CHECK(rdp_test_depth({0x37ffc, 0}, 0x6000, 0, 8, 7, modes).pass);
    CHECK(rdp_test_depth({0x37ff8, 0}, 0x6000, 0, 8, 7, modes).pass);
    CHECK(rdp_test_depth({0x38008, 0}, 0x6000, 0, 8, 7, modes).pass);
    CHECK(!rdp_test_depth({0x37ff7, 0}, 0x6000, 0, 8, 7, modes).pass);
    CHECK(!rdp_test_depth({0x38009, 0}, 0x6000, 0, 8, 7, modes).pass);
}

TEST(rdp_depth_low_precision_maximum_delta_forces_coplanar_window) {
    const u64 decal = compare | (3ULL << 10U);
    CHECK(rdp_test_depth({0x3ffff, 0}, 0x0003, 3, 8, 7, decal).pass);
    CHECK(rdp_test_depth({0, 0}, 0x4003, 3, 8, 7, decal).pass);
    CHECK(!rdp_test_depth({0, 0}, 0x6003, 2, 8, 7, decal).pass);
    CHECK(rdp_test_depth({0x3ffff, 0}, 0x0003, 3, 4, 0, compare | aa).blend_enabled);
    CHECK(!rdp_test_depth({0x3ffff, 0}, 0x0003, 3, 8, 0, compare).pass);
}

TEST(rdp_depth_interpenetrating_mode_scales_and_caps_coverage) {
    const u64 modes = compare | (1ULL << 10U);
    for (unsigned difference = 0; difference <= 8; ++difference) {
        const auto result = rdp_test_depth({0x38000U - difference * 8U, 8}, 0x6000, 0, 8, 7, modes);
        CHECK_EQ(result.pass, difference != 0U);
        if (difference != 0U)
            CHECK_EQ(result.coverage, difference);
        CHECK(result.coverage_wrap);
    }
    const auto fractional = rdp_test_depth({0x37fe8, 8}, 0x6000, 0, 4, 7, modes);
    CHECK(fractional.pass);
    CHECK_EQ(fractional.coverage, 1U);
    const auto capped = rdp_test_depth({0x37ff1, 0}, 0x6000, 0, 8, 7, modes);
    CHECK(capped.pass);
    CHECK_EQ(capped.coverage, 8U);
}

TEST(rdp_depth_interpenetrating_coverage_can_reach_zero) {
    const auto result = rdp_test_depth({0x37fff, 8}, 0x5ffc, 0, 4, 7, compare | (1ULL << 10U));
    CHECK(!result.pass);
    const auto zero = rdp_test_depth({0x3c003, 8}, 0x8004, 0, 4, 7, compare | (1ULL << 10U));
    CHECK(zero.pass);
    CHECK_EQ(zero.coverage, 0U);
    CHECK(zero.coverage_wrap);
}

TEST(rdp_depth_hidden_delta_bits_control_blend_shifts) {
    for (unsigned hidden = 0; hidden < 4; ++hidden) {
        const auto result = rdp_test_depth({0, 0x80}, 0x6001, static_cast<u8>(hidden), 8, 7, compare);
        CHECK_EQ(result.pixel_alpha_shift, 3U - hidden);
        CHECK_EQ(result.memory_alpha_shift, 0U);
    }
    const auto memory = rdp_test_depth({0, 1}, 0x6003, 3, 8, 7, compare);
    CHECK_EQ(memory.pixel_alpha_shift, 0U);
    CHECK_EQ(memory.memory_alpha_shift, 4U);
}

TEST(rdp_depth_blender_shifts_pixel_alpha_before_masking) {
    const u64 modes = force | (1ULL << 22U) | (1ULL << 18U);
    constexpr unsigned expected[] = {144, 104, 84, 64, 64};
    for (unsigned shift = 0; shift < 5; ++shift)
        CHECK_EQ(rdp_blend({}, modes, {160, 160, 160, 128}, {64, 64, 64, 224}, 0, true, true, 0, shift)[0],
                 static_cast<s32>(expected[shift]));
}
