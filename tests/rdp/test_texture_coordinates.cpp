#include "cupid/rdp/texture_coordinates.hpp"
#include "test.hpp"

#include <array>
#include <bit>

namespace {
using namespace cupid;
}

TEST(rdp_perspective_power_of_two_divisors_and_signed_saturation) {
    for (unsigned bit = 0; bit < 15; ++bit) {
        const auto w = static_cast<s16>(1U << bit);
        const s32 scale = static_cast<s32>(1U << (15U - bit));
        for (s32 value = -32768; value <= 32767; value += 127) {
            const s32 quotient = value * scale;
            const s32 expected = quotient < -32768 ? -32768 : quotient > 32767 ? 32767 : quotient;
            CHECK_EQ(rdp_perspective_coordinate(static_cast<s16>(value), w), expected);
        }
    }
}

TEST(rdp_perspective_nonpositive_divisors_force_positive_saturation) {
    for (s32 w = -32768; w <= 0; ++w)
        for (s16 coordinate : std::array<s16, 5>{-32768, -1, 0, 1, 32767})
            CHECK_EQ(rdp_perspective_coordinate(coordinate, static_cast<s16>(w)), 32767);
}

TEST(rdp_perspective_fractional_reciprocals_keep_rom_precision) {
    struct Case {
        s16 coordinate, w, expected;
    };
    for (const auto& c : {Case{473, 0x1501, 2882}, Case{-473, 0x1501, -2883}, Case{2047, 0x4617, 3738},
                          Case{-2047, 0x4617, -3739}, Case{8191, 0x7fff, 8191}, Case{8191, 0x46ff, 14767},
                          Case{1537, 0x557b, 2301}, Case{-1537, 0x557b, -2302}, Case{1024, 0x2345, 3716}})
        CHECK_EQ(rdp_perspective_coordinate(c.coordinate, c.w), c.expected);
}

TEST(rdp_perspective_zero_numerator_stays_zero_for_every_positive_divisor) {
    for (s32 w = 1; w <= 32767; ++w)
        CHECK_EQ(rdp_perspective_coordinate(0, static_cast<s16>(w)), 0);
}
