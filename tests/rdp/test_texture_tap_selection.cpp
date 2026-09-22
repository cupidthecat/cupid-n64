#include "cupid/rdp/texture_sampling.hpp"
#include "test.hpp"

namespace {
using namespace cupid;

struct YuvSample {
    std::array<u8, 4096> memory{};
    RdpTile tile{};

    explicit YuvSample(unsigned first_x) {
        tile.format = 1;
        tile.size = 2;
        tile.line_stride = 8;
        tile.s_mask = tile.t_mask = 3;
        tile.s_high = tile.t_high = 28;
        const auto chroma = [&](unsigned address, int u, int v) {
            memory[address] = static_cast<u8>(u + 128);
            memory[address + 1U] = static_cast<u8>(v + 128);
        };
        chroma(0, 10, 20);
        chroma(2, 50, 80);
        chroma(12, 90, 120);
        chroma(14, -100, -60);
        memory[2048U + first_x] = 30;
        memory[2049U + first_x] = 70;
        memory[2048U + ((8U + first_x) ^ 4U)] = 110;
        memory[2048U + ((9U + first_x) ^ 4U)] = 250;
    }
};
} // namespace

TEST(rdp_sampler_yuv_chroma_and_luma_can_use_opposite_filter_triangles) {
    constexpr u64 modes = (1ULL << 45U) | (1ULL << 43U);
    const YuvSample even(0);
    CHECK_EQ(rdp_sample_texture(even.memory, even.tile, {24, 16}, modes, 0, {}),
             (RdpColor{65, 93, 125, 125}));
    const YuvSample odd(1);
    CHECK_EQ(rdp_sample_texture(odd.memory, odd.tile, {40, 16}, modes, 0, {}), (RdpColor{46, 78, 80, 80}));
}

TEST(rdp_sampler_yuv_mid_texel_filter_can_apply_to_only_chroma_or_luma) {
    constexpr u64 modes = (1ULL << 45U) | (1ULL << 44U) | (1ULL << 43U);
    const YuvSample odd(1);
    CHECK_EQ(rdp_sample_texture(odd.memory, odd.tile, {32, 16}, modes, 0, {}), (RdpColor{13, 40, 70, 70}));
    const YuvSample even(0);
    CHECK_EQ(rdp_sample_texture(even.memory, even.tile, {16, 16}, modes, 0, {}),
             (RdpColor{60, 85, 115, 115}));
}

TEST(rdp_sampler_yuv_unfiltered_planes_select_their_own_base_before_conversion) {
    constexpr std::array<u16, 6> convert{127, 448, 448, 255, 0, 0};
    constexpr u64 modes = 1ULL << 45U;
    const YuvSample even(0);
    CHECK_EQ(rdp_sample_texture(even.memory, even.tile, {24, 16}, modes, 0, convert),
             (RdpColor{270, 235, 270, 250}));
    const YuvSample odd(1);
    CHECK_EQ(rdp_sample_texture(odd.memory, odd.tile, {40, 16}, modes, 0, convert),
             (RdpColor{-30, 109, -170, 30}));
}

TEST(rdp_sampler_convert_one_without_quad_retains_signed_nine_bit_blue) {
    YuvSample source(0);
    source.memory.fill(0xff);
    constexpr u64 modes = (1ULL << 42U) | (1ULL << 41U);
    for (unsigned format : {0U, 1U, 2U, 3U, 4U}) {
        source.tile.format = static_cast<u8>(format);
        CHECK_EQ(
            rdp_sample_texture(source.memory, source.tile, {65535, -65536}, modes, 1, {}, {255, 256, 511, 0}),
            (RdpColor{-1, -1, -1, -1}));
    }
}

TEST(rdp_sampler_yuv_convert_one_keeps_separate_chroma_and_luma_footprints) {
    constexpr u64 modes = (1ULL << 45U) | (1ULL << 42U) | (1ULL << 41U);
    constexpr RdpColor previous{128, 64, 100, 255};
    const YuvSample even(0);
    const YuvSample odd(1);
    CHECK_EQ(rdp_sample_texture(even.memory, even.tile, {24, 16}, modes, 1, {}, previous),
             (RdpColor{140, 155, -15, -15}));
    CHECK_EQ(rdp_sample_texture(odd.memory, odd.tile, {40, 16}, modes, 1, {}, previous),
             (RdpColor{233, 225, 140, 140}));
    constexpr u64 mid = modes | (1ULL << 44U);
    CHECK_EQ(rdp_sample_texture(odd.memory, odd.tile, {32, 16}, mid, 1, {}, previous),
             (RdpColor{260, 245, 140, 140}));
    CHECK_EQ(rdp_sample_texture(even.memory, even.tile, {16, 16}, mid, 1, {}, previous),
             (RdpColor{140, 155, -70, -70}));
}
