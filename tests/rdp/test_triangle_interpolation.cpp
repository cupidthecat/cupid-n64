#include "cupid/rdp/texture_coordinates.hpp"
#include "cupid/rdp/triangle.hpp"
#include "test.hpp"

using namespace cupid;

TEST(rdp_triangle_origin_retains_negative_edges_and_fractional_bits) {
    const auto geometry =
        rdp_triangle_geometry({(1ULL << 55U) | 5U, 0, (0xfffe8003ULL << 32U) | 0x10000U, 0});
    const auto before = rdp_triangle_origin(geometry, 2);
    CHECK_EQ(before.x, -1);
    CHECK_EQ(before.fraction, 128U);
    CHECK_EQ(before.rows, 1);
    const auto after = rdp_triangle_origin(geometry, 3);
    CHECK_EQ(after.x, 0);
    CHECK_EQ(after.fraction, 128U);
    CHECK_EQ(after.rows, 2);
}

TEST(rdp_triangle_origin_latches_last_subpixel_for_matching_slope_and_direction) {
    const auto right_major = rdp_triangle_geometry({0, 0, 0x10000, 0});
    const auto last = rdp_triangle_origin(right_major, 0);
    CHECK(last.offset_latch);
    CHECK_EQ(last.x, 0);
    CHECK_EQ(last.fraction, 192U);
    const auto left_major = rdp_triangle_geometry({1ULL << 55U, 0, 0x10000, 0});
    CHECK(!rdp_triangle_origin(left_major, 0).offset_latch);
}

TEST(rdp_triangle_varying_base_truncates_edge_and_fractional_corrections) {
    const RdpVarying varying{0x12345678, 0x40004, 0x10001, 0x20003};
    CHECK_EQ(rdp_varying_base(varying, {.rows = 2, .fraction = 128}), 0x12345400U);
    CHECK_EQ(rdp_varying_base(varying, {.rows = 2, .fraction = 128, .offset_latch = true}), 0x12339400U);
}

TEST(rdp_triangle_shade_centroid_combines_horizontal_and_vertical_derivatives) {
    const RdpVarying varying{100U << 16U, 32U << 16U, 0, 64U << 16U};
    CHECK_EQ(rdp_interpolate_shade(varying.value, varying, 1, 8), 172);
    CHECK_EQ(rdp_interpolate_shade(varying.value, varying, 1, 1), 132);
}

TEST(rdp_triangle_shade_clamp_uses_nine_bit_overflow_regions) {
    constexpr s32 values[] = {0, 255, 256, 383, 384, 511, 512, -1, -128, -129, -256};
    constexpr s32 expected[] = {0, 255, 255, 255, 0, 0, 0, 0, 0, 255, 255};
    for (unsigned i = 0; i < std::size(values); ++i) {
        const RdpVarying varying{static_cast<u32>(values[i]) << 16U, 0, 0, 0};
        CHECK_EQ(rdp_interpolate_shade(varying.value, varying, 0, 255), expected[i]);
    }
}

TEST(rdp_triangle_shade_truncates_horizontal_step_low_bits) {
    const RdpVarying varying{0, 31, 0, 0};
    CHECK_EQ(rdp_interpolate_shade(0, varying, 65536, 255), 0);
}

TEST(rdp_triangle_depth_centroid_preserves_more_precision_than_color) {
    const RdpVarying varying{0x40000000, 0x10000, 0, 0x20000};
    CHECK_EQ(rdp_interpolate_depth(varying.value, varying, 0, 8), 0x2000aU);
    CHECK_EQ(rdp_interpolate_depth(varying.value, varying, 0, 1), 0x20000U);
}

TEST(rdp_triangle_depth_clamp_distinguishes_overflow_and_underflow) {
    constexpr u32 inputs[] = {0, 0x7fffffff, 0x80000000, 0xbfffffff, 0xc0000000, 0xffffffff};
    constexpr u32 expected[] = {0, 0x3ffff, 0x3ffff, 0x3ffff, 0, 0};
    for (unsigned i = 0; i < std::size(inputs); ++i)
        CHECK_EQ(rdp_interpolate_depth(inputs[i], {}, 0, 1), expected[i]);
}

TEST(rdp_triangle_depth_delta_uses_ones_complement_and_next_power) {
    CHECK_EQ(rdp_triangle_depth_delta({}), 1U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0x10000}), 2U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0x20000}), 4U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0x10000, .dy = 0x20000}), 4U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0xffff0000}), 1U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0xfffe0000}), 2U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0x7fff0000, .dy = 0x10000}), 0x8000U);
    CHECK_EQ(rdp_triangle_depth_delta({.dx = 0x7fff0000, .dy = 0x7fff0000}), 0x8000U);
}

TEST(rdp_perspective_wide_retains_seventeen_bits_for_lod) {
    bool overflow = false;
    CHECK_EQ(rdp_perspective_coordinate_wide(20000, 0x4000, overflow), 40000);
    CHECK_EQ(rdp_perspective_coordinate_wide(-20000, 0x4000, overflow), -40000);
    CHECK_EQ(rdp_perspective_coordinate_wide(16000, 0x2000, overflow), 64000);
    CHECK_EQ(rdp_perspective_coordinate_wide(-16000, 0x2000, overflow), -64000);
    CHECK(!overflow);
    CHECK_EQ(rdp_perspective_coordinate(20000, 0x4000), 32767);
}

TEST(rdp_perspective_wide_reports_overflow_and_nonpositive_w) {
    bool overflow = false;
    CHECK_EQ(rdp_perspective_coordinate_wide(20000, 0x2000, overflow), 32767);
    CHECK(overflow);
    overflow = false;
    CHECK_EQ(rdp_perspective_coordinate_wide(-20000, 0x2000, overflow), -32768);
    CHECK(overflow);
    for (s16 w : {s16{0}, s16{-1}}) {
        overflow = false;
        CHECK_EQ(rdp_perspective_coordinate_wide(0, w, overflow), 32767);
        CHECK(overflow);
    }
    CHECK_EQ(rdp_perspective_coordinate_wide(0, 0x4000, overflow), 0);
    CHECK(overflow);
}
