#include "cupid/rdp/texture_coordinates.hpp"
#include "test.hpp"

#include <array>

namespace {
using namespace cupid;
}

TEST(rdp_perspective_point_preserves_wide_results_and_reciprocal_rounding) {
    struct Case {
        s16 s, t, w;
        s32 expected_s, expected_t;
    };
    constexpr std::array cases{
        Case{20000, -20000, 0x4000, 40000, -40000}, Case{16000, -16000, 0x2000, 64000, -64000},
        Case{473, -473, 0x1501, 2882, -2883},       Case{2047, -2047, 0x4617, 3738, -3739},
        Case{1537, -1537, 0x557b, 2301, -2302},     Case{8191, 0, 0x7fff, 8191, 0},
    };
    for (const auto& c : cases) {
        bool overflow = false;
        const auto point = rdp_perspective_point(c.s, c.t, c.w, overflow);
        CHECK_EQ(point[0], c.expected_s);
        CHECK_EQ(point[1], c.expected_t);
        CHECK(!overflow);
    }
}

TEST(rdp_perspective_point_accumulates_overflow_from_either_axis) {
    for (bool initial : {false, true}) {
        bool overflow = initial;
        const auto first = rdp_perspective_point(20000, 0, 0x2000, overflow);
        CHECK_EQ(first[0], 32767);
        CHECK_EQ(first[1], 0);
        CHECK(overflow);
        overflow = initial;
        const auto second = rdp_perspective_point(0, -20000, 0x2000, overflow);
        CHECK_EQ(second[0], 0);
        CHECK_EQ(second[1], -32768);
        CHECK(overflow);
        overflow = initial;
        const auto normal = rdp_perspective_point(0, 1, 0x4000, overflow);
        CHECK_EQ(normal[0], 0);
        CHECK_EQ(normal[1], 2);
        CHECK_EQ(overflow, initial);
    }
    for (s32 w = -32768; w <= 0; ++w) {
        bool overflow = false;
        const auto point = rdp_perspective_point(-32768, 32767, static_cast<s16>(w), overflow);
        CHECK_EQ(point[0], 32767);
        CHECK_EQ(point[1], 32767);
        CHECK(overflow);
    }
}

TEST(rdp_perspective_point_matches_separate_axes_for_every_positive_divisor) {
    constexpr std::array<s16, 9> coordinates{-32768, -20000, -16384, -1, 0, 1, 16383, 20000, 32767};
    for (s32 w = 1; w <= 32767; ++w) {
        for (std::size_t index = 0; index < coordinates.size(); ++index) {
            const s16 s = coordinates[index];
            const s16 t = coordinates[coordinates.size() - 1U - index];
            bool expected_overflow = false;
            const std::array<s32, 2> expected{
                rdp_perspective_coordinate_wide(s, static_cast<s16>(w), expected_overflow),
                rdp_perspective_coordinate_wide(t, static_cast<s16>(w), expected_overflow)};
            bool actual_overflow = false;
            const auto actual = rdp_perspective_point(s, t, static_cast<s16>(w), actual_overflow);
            CHECK_EQ(actual, expected);
            CHECK_EQ(actual_overflow, expected_overflow);
        }
    }
}
