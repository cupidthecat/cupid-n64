#include "cupid/rdp/texture_coordinates.hpp"
#include "cupid/rdp/texture_sampling.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <bit>

using namespace cupid;

namespace {

constexpr std::array<s32, 64> divider_bases{
    0x4000, 0x3f04, 0x3e10, 0x3d22, 0x3c3c, 0x3b5d, 0x3a83, 0x39b1, 0x38e4, 0x381c, 0x375a, 0x369d, 0x35e5,
    0x3532, 0x3483, 0x33d9, 0x3333, 0x3291, 0x31f4, 0x3159, 0x30c3, 0x3030, 0x2fa1, 0x2f15, 0x2e8c, 0x2e06,
    0x2d83, 0x2d03, 0x2c86, 0x2c0b, 0x2b93, 0x2b1e, 0x2aab, 0x2a3a, 0x29cc, 0x2960, 0x28f6, 0x288e, 0x2828,
    0x27c4, 0x2762, 0x2702, 0x26a4, 0x2648, 0x25ed, 0x2594, 0x253d, 0x24e7, 0x2492, 0x243f, 0x23ee, 0x239e,
    0x234f, 0x2302, 0x22b6, 0x226c, 0x2222, 0x21da, 0x2193, 0x214d, 0x2108, 0x20c5, 0x2082, 0x2041};

constexpr std::array<s32, 64> divider_decrements{
    252, 244, 238, 230, 223, 218, 210, 205, 200, 194, 189, 184, 179, 175, 170, 166,
    162, 157, 155, 150, 147, 143, 140, 137, 134, 131, 128, 125, 123, 120, 117, 115,
    113, 110, 108, 106, 104, 102, 100, 98,  96,  94,  92,  91,  89,  87,  86,  85,
    83,  81,  80,  79,  77,  76,  74,  74,  72,  71,  70,  69,  67,  67,  65,  65};

struct DividerOracle {
    s64 reciprocal;
    unsigned shift;

    explicit DividerOracle(unsigned w) : reciprocal(0), shift(0) {
        while (w < 0x4000U) {
            w *= 2U;
            ++shift;
        }
        const unsigned segment = (w - 0x4000U) / 256U;
        const s64 decrement = static_cast<s64>(divider_decrements[segment]) * (w % 256U);
        reciprocal = divider_bases[segment] - (decrement + 255) / 256;
    }

    s32 divide(s16 coordinate, bool& overflow) const {
        const s64 product = coordinate * reciprocal;
        const s64 limit = 1LL << (29U - shift);
        if (product < -limit || product >= limit) {
            overflow = true;
            return product < 0 ? -32768 : 32767;
        }
        const s64 numerator = product * (1LL << shift);
        const s64 quotient = numerator >= 0 ? numerator / 8192 : -((-numerator + 8191) / 8192);
        return static_cast<s32>(std::clamp(quotient, s64{-65536}, s64{65535}));
    }
};

void store_word(std::array<u8, 4096>& memory, unsigned address, u16 word) {
    memory[address] = static_cast<u8>(word >> 8U);
    memory[address + 1U] = static_cast<u8>(word);
}

} // namespace

TEST(rdp_rgba16_lookup_preserves_every_color_word_and_tlut_decode) {
    std::array<u8, 4096> memory{};
    RdpTile tile;
    tile.format = 0;
    tile.size = 2;
    tile.s_high = tile.t_high = 4;
    constexpr u64 filter = 1ULL << 43U;
    for (unsigned word = 0; word < 65536U; ++word) {
        const auto red = static_cast<s32>((word / 2048U) % 32U);
        const auto green = static_cast<s32>((word / 64U) % 32U);
        const auto blue = static_cast<s32>((word / 2U) % 32U);
        const RdpColor expected{red * 8 + red / 4, green * 8 + green / 4, blue * 8 + blue / 4,
                                static_cast<s32>(word % 2U) * 255};
        store_word(memory, 0, static_cast<u16>(word));
        CHECK_EQ(rdp_sample_texture(memory, tile, {0, 0}, filter, 0, {}), expected);
        store_word(memory, 0, 0x0300);
        store_word(memory, 2072, static_cast<u16>(word));
        CHECK_EQ(rdp_sample_texture(memory, tile, {0, 0}, filter | (1ULL << 47U), 0, {}), expected);
    }
}

TEST(rdp_perspective_lookup_matches_divider_rom_for_all_positive_divisors) {
    constexpr std::array<s16, 18> coordinates{-32768, -32767, -16384, -8193, -8192, -257,  -256,  -1,   0, 1,
                                              255,    256,    8191,   8192,  16383, 16384, 32766, 32767};
    unsigned compared = 0;
    for (unsigned w = 1; w < 32768U; ++w) {
        const DividerOracle oracle(w);
        for (unsigned index = 0; index < coordinates.size() + 2U; ++index) {
            const s16 s = index < coordinates.size()
                              ? coordinates[index]
                              : std::bit_cast<s16>(static_cast<u16>(w * 40503U + index * 997U));
            const s16 t = std::bit_cast<s16>(static_cast<u16>(static_cast<u16>(s) ^ 0x9a37U));
            for (const bool sticky : {false, true}) {
                bool expected_overflow = sticky;
                const std::array<s32, 2> expected{oracle.divide(s, expected_overflow),
                                                  oracle.divide(t, expected_overflow)};
                bool actual_overflow = sticky;
                CHECK_EQ(rdp_perspective_point(s, t, static_cast<s16>(w), actual_overflow), expected);
                CHECK_EQ(actual_overflow, expected_overflow);
                actual_overflow = sticky;
                expected_overflow = sticky;
                CHECK_EQ(rdp_perspective_coordinate_wide(s, static_cast<s16>(w), actual_overflow),
                         oracle.divide(s, expected_overflow));
                CHECK_EQ(actual_overflow, expected_overflow);
                CHECK_EQ(rdp_perspective_coordinate(s, static_cast<s16>(w)),
                         std::clamp(expected[0], -32768, 32767));
                ++compared;
            }
        }
    }
    CHECK_EQ(compared, 1310680U);
}
