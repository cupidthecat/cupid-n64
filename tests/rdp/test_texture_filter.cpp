#include "cupid/rdp/texture_sampling.hpp"
#include "test.hpp"

using namespace cupid;

TEST(rdp_texture_rgba32_filter_matches_barycentric_weights_for_all_fraction_pairs) {
    RdpTile tile{};
    tile.format = 0;
    tile.size = 3;
    tile.line_stride = 8;
    tile.s_mask = tile.t_mask = 1;
    tile.s_high = tile.t_high = 4;
    const std::array<u16, 6> convert{};
    u32 random = 0x90eab123U;
    for (unsigned pattern = 0; pattern < 256U; ++pattern) {
        std::array<RdpColor, 4> taps{};
        std::array<u8, 4096> memory{};
        for (unsigned tap = 0; tap < taps.size(); ++tap) {
            for (auto& channel : taps[tap]) {
                random ^= random << 13U;
                random ^= random >> 17U;
                random ^= random << 5U;
                channel = static_cast<s32>(pattern < 16U ? ((pattern >> tap) & 1U) * 255U : random & 255U);
            }
            const unsigned address = tap < 2U ? tap * 2U : 12U + (tap - 2U) * 2U;
            write_be16(memory.data() + address, static_cast<u16>(taps[tap][0] * 256 + taps[tap][1]));
            write_be16(memory.data() + address + 2048U, static_cast<u16>(taps[tap][2] * 256 + taps[tap][3]));
        }
        for (s32 s = 0; s < 32; ++s) {
            for (s32 t = 0; t < 32; ++t) {
                for (const bool mid : {false, true}) {
                    RdpColor expected{};
                    for (unsigned channel = 0; channel < expected.size(); ++channel) {
                        if (mid && s == 16 && t == 16) {
                            expected[channel] = (taps[0][channel] + taps[1][channel] + taps[2][channel] +
                                                 taps[3][channel] + 2) /
                                                4;
                        } else {
                            const std::array<s32, 4> weights =
                                s + t < 32 ? std::array<s32, 4>{32 - s - t, s, t, 0}
                                           : std::array<s32, 4>{0, 32 - t, 32 - s, s + t - 32};
                            s32 weighted = 16;
                            for (unsigned tap = 0; tap < taps.size(); ++tap)
                                weighted += taps[tap][channel] * weights[tap];
                            expected[channel] = weighted / 32;
                        }
                    }
                    const u64 modes = (1ULL << 45U) | (1ULL << 43U) | (mid ? 1ULL << 44U : 0U);
                    CHECK_EQ(rdp_sample_texture(memory, tile, {s, t}, modes, 0U, convert), expected);
                }
            }
        }
    }
}
