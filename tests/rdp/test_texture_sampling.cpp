#include "cupid/rdp/texture_sampling.hpp"
#include "test.hpp"

using namespace cupid;

namespace {
struct Sampler {
    std::array<u8, 4096> memory{};
    RdpTile tile{};
    std::array<u16, 6> convert{};
    u64 modes = 1ULL << 43U;

    Sampler() {
        tile.format = 4;
        tile.size = 1;
        tile.line_stride = 8;
        tile.s_mask = tile.t_mask = 3;
        tile.s_high = tile.t_high = 28;
        memory[0] = 0;
        memory[1] = 64;
        memory[12] = 128;
        memory[13] = 240;
    }
    void word(unsigned address, unsigned value) {
        memory[address] = static_cast<u8>(value >> 8U);
        memory[address + 1U] = static_cast<u8>(value);
    }
    RdpColor sample(s32 s = 0, s32 t = 0, unsigned cycle = 0, RdpColor previous = {}) const {
        return rdp_sample_texture(memory, tile, {s, t}, modes, cycle, convert, previous);
    }
};
} // namespace

TEST(rdp_sampler_four_bit_formats_decode_intensity_alpha_and_palette) {
    Sampler sample;
    sample.tile.size = 0;
    sample.memory[0] = 0xab;
    const RdpColor expected[] = {
        {170, 170, 170, 170}, {}, {58, 58, 58, 58}, {182, 182, 182, 0}, {170, 170, 170, 170}};
    sample.tile.palette = 3;
    for (unsigned format : {0U, 2U, 3U, 4U}) {
        sample.tile.format = static_cast<u8>(format);
        CHECK_EQ(sample.sample(), expected[format]);
    }
    sample.tile.format = 3;
    CHECK_EQ(sample.sample(32), (RdpColor{182, 182, 182, 255}));
}

TEST(rdp_sampler_eight_bit_formats_decode_both_nibbles) {
    Sampler sample;
    sample.memory[0] = 0xab;
    for (unsigned format : {0U, 2U, 4U}) {
        sample.tile.format = static_cast<u8>(format);
        CHECK_EQ(sample.sample(), (RdpColor{171, 171, 171, 171}));
    }
    sample.tile.format = 3;
    CHECK_EQ(sample.sample(), (RdpColor{170, 170, 170, 187}));
}

TEST(rdp_sampler_sixteen_bit_formats_expand_or_route_halfword_channels) {
    Sampler sample;
    sample.tile.size = 2;
    sample.word(0, 0xab35);
    const RdpColor expected[] = {
        {173, 99, 214, 255}, {}, {171, 53, 171, 53}, {171, 171, 171, 53}, {171, 53, 171, 53}};
    for (unsigned format : {0U, 2U, 3U, 4U}) {
        sample.tile.format = static_cast<u8>(format);
        CHECK_EQ(sample.sample(), expected[format]);
    }
}

TEST(rdp_sampler_rgba32_reads_split_tmem_banks) {
    Sampler sample;
    sample.tile.size = 3;
    sample.tile.format = 0;
    sample.word(0, 0xab35);
    sample.word(2048, 0xcdef);
    CHECK_EQ(sample.sample(), (RdpColor{171, 53, 205, 239}));
    for (unsigned format : {2U, 3U, 4U}) {
        sample.tile.format = static_cast<u8>(format);
        CHECK_EQ(sample.sample(), (RdpColor{171, 53, 171, 53}));
    }
}

TEST(rdp_sampler_point_reads_swap_odd_rows_and_wrap_tmem) {
    Sampler sample;
    CHECK_EQ(sample.sample(0, 32)[0], 128);
    sample.tile.tmem_address = 4088;
    sample.tile.s_mask = 4;
    sample.memory[0] = 37;
    sample.memory[4] = 91;
    CHECK_EQ(sample.sample(256, 0)[0], 37);
    CHECK_EQ(sample.sample(0, 32)[0], 91);
}

TEST(rdp_sampler_rgba32_wraps_within_each_bank) {
    Sampler sample;
    sample.tile.format = 0;
    sample.tile.size = 3;
    sample.tile.tmem_address = 2040;
    sample.word(0, 0x1234);
    sample.word(2048, 0x5678);
    CHECK_EQ(sample.sample(128, 0), (RdpColor{18, 52, 86, 120}));
}

TEST(rdp_sampler_three_tap_filter_uses_both_triangular_halves) {
    Sampler sample;
    sample.modes |= 1ULL << 45U;
    CHECK_EQ(sample.sample(8, 8)[0], 48);
    CHECK_EQ(sample.sample(24, 24)[0], 168);
    CHECK_EQ(sample.sample(16, 16)[0], 96);
    sample.modes |= 1ULL << 44U;
    CHECK_EQ(sample.sample(16, 16)[0], 108);
    CHECK_EQ(sample.sample(8, 8)[0], 48);
}

TEST(rdp_sampler_filter_rounds_half_steps_up) {
    Sampler sample;
    sample.memory[1] = 1;
    sample.modes |= 1ULL << 45U;
    CHECK_EQ(sample.sample(15, 0)[0], 0);
    CHECK_EQ(sample.sample(16, 0)[0], 1);
}

TEST(rdp_sampler_mask_and_mirror_apply_to_each_filter_tap) {
    Sampler sample;
    sample.tile.s_mask = 1;
    sample.modes |= 1ULL << 45U;
    CHECK_EQ(sample.sample(48, 0)[0], 32);
    sample.tile.s_mirror = true;
    CHECK_EQ(sample.sample(48, 0)[0], 64);
    CHECK_EQ(sample.sample(-16, 0)[0], 0);
}

TEST(rdp_sampler_clamp_includes_tile_origin_and_clears_fraction) {
    Sampler sample;
    sample.tile.s_clamp = true;
    sample.tile.s_low = 4;
    sample.tile.s_high = 8;
    sample.modes |= 1ULL << 45U;
    CHECK_EQ(sample.sample(-32)[0], 0);
    CHECK_EQ(sample.sample(48)[0], 32);
    CHECK_EQ(sample.sample(64)[0], 64);
    CHECK_EQ(sample.sample(200)[0], 64);
}

TEST(rdp_sampler_zero_mask_forces_clamping) {
    Sampler sample;
    sample.tile.s_mask = 0;
    sample.tile.s_high = 4;
    CHECK_EQ(sample.sample(1024)[0], 64);
    CHECK_EQ(sample.sample(-32)[0], 0);
}

TEST(rdp_sampler_coordinate_shifts_keep_signed_sixteen_bit_wrap) {
    Sampler sample;
    sample.tile.s_shift = 1;
    CHECK_EQ(sample.sample(64)[0], 64);
    sample.tile.s_shift = 15;
    CHECK_EQ(sample.sample(16)[0], 64);
    sample.tile.s_shift = 11;
    CHECK_EQ(sample.sample(1025)[0], 64);
    sample.tile.s_clamp = true;
    sample.memory[7] = 93;
    CHECK_EQ(sample.sample(1025)[0], 0);
}

TEST(rdp_sampler_coordinates_saturate_before_shifting) {
    Sampler sample;
    sample.tile.s_shift = 10;
    sample.memory[7] = 123;
    sample.memory[6] = 211;
    CHECK_EQ(sample.sample(65535)[0], 0);
    CHECK_EQ(sample.sample(-65536)[0], 123);
}

TEST(rdp_sampler_t_address_wrap_retains_next_row_carry) {
    Sampler sample;
    sample.tile.t_mask = 10;
    sample.memory[0] = 32;
    sample.memory[2044] = 10;
    sample.memory[2048] = 200;
    CHECK_EQ(sample.sample(0, 8192)[0], 32);
    sample.modes |= 1ULL << 45U;
    CHECK_EQ(sample.sample(0, 8176)[0], 105);
}

TEST(rdp_sampler_masks_above_ten_bits_use_ten_bits) {
    Sampler sample;
    sample.tile.s_mask = 15;
    sample.tile.s_shift = 15;
    sample.memory[0] = 32;
    sample.memory[1024] = 200;
    CHECK_EQ(sample.sample(16384)[0], 32);
}

TEST(rdp_sampler_tlut_four_bit_indices_include_palette) {
    Sampler sample;
    sample.tile.size = 0;
    sample.tile.format = 2;
    sample.tile.palette = 5;
    sample.memory[0] = 0x30;
    sample.word(2048 + 83 * 8, 0xf801);
    sample.modes |= 1ULL << 47U;
    CHECK_EQ(sample.sample(), (RdpColor{255, 0, 0, 255}));
    sample.modes |= 1ULL << 46U;
    CHECK_EQ(sample.sample(), (RdpColor{248, 248, 248, 1}));
}

TEST(rdp_sampler_tlut_large_indices_use_high_byte_and_lower_bank) {
    Sampler sample;
    sample.tile.tmem_address = 2048;
    sample.word(0, 0x03ab);
    sample.word(2048 + 3 * 8, 0x07c1);
    sample.modes |= 1ULL << 47U;
    for (unsigned size : {1U, 2U, 3U}) {
        sample.tile.size = static_cast<u8>(size);
        CHECK_EQ(sample.sample(), (RdpColor{0, 255, 0, 255}));
    }
}

TEST(rdp_sampler_tlut_filter_reads_four_palette_banks_without_quad) {
    Sampler sample;
    sample.tile.format = 2;
    sample.memory[0] = 2;
    sample.word(2064, 0x00ff);
    sample.word(2066, 0x40ff);
    sample.word(2068, 0x80ff);
    sample.word(2070, 0xf0ff);
    sample.modes |= (1ULL << 47U) | (1ULL << 46U);
    CHECK_EQ(sample.sample(8, 8)[0], 48);
    CHECK_EQ(sample.sample(24, 24)[0], 48);
    sample.modes |= 1ULL << 44U;
    CHECK_EQ(sample.sample(16, 16)[0], 108);
}

TEST(rdp_sampler_tlut_quad_looks_up_each_texel_index) {
    Sampler sample;
    sample.tile.format = 2;
    sample.memory[0] = 0;
    sample.memory[1] = 1;
    sample.memory[12] = 2;
    sample.memory[13] = 3;
    const unsigned colors[] = {0, 64, 128, 240};
    for (unsigned index = 0; index < 4; ++index)
        for (unsigned bank = 0; bank < 4; ++bank)
            sample.word(2048 + index * 8 + bank * 2, colors[index] * 256U + 255U);
    sample.modes |= (1ULL << 47U) | (1ULL << 46U) | (1ULL << 45U);
    CHECK_EQ(sample.sample(8, 8)[0], 48);
    CHECK_EQ(sample.sample(24, 24)[0], 168);
}

TEST(rdp_sampler_yuv_uses_signed_chroma_and_separate_luma_bank) {
    Sampler sample;
    sample.tile.format = 1;
    sample.tile.size = 2;
    sample.word(0, 0x8a94);
    sample.memory[2048] = 100;
    CHECK_EQ(sample.sample(), (RdpColor{10, 20, 100, 100}));
    sample.convert = {127, 448, 448, 255, 0, 0};
    sample.modes = 0;
    CHECK_EQ(sample.sample(), (RdpColor{120, 85, 120, 100}));
}

TEST(rdp_sampler_yuv_chroma_filter_has_half_horizontal_rate) {
    Sampler sample;
    sample.tile.format = 1;
    sample.tile.size = 2;
    sample.word(0, 0x8080);
    sample.word(2, 0xc0a0);
    sample.memory[2048] = 100;
    sample.memory[2049] = 120;
    sample.modes |= 1ULL << 45U;
    CHECK_EQ(sample.sample(32, 0), (RdpColor{32, 16, 120, 120}));
}

TEST(rdp_sampler_conversion_applies_to_non_yuv_formats) {
    Sampler sample;
    sample.tile.format = 0;
    sample.tile.size = 3;
    sample.word(0, 0x0a14);
    sample.word(2048, 0x64ff);
    sample.convert = {127, 448, 448, 255, 0, 0};
    sample.modes = 0;
    CHECK_EQ(sample.sample(), (RdpColor{120, 85, 120, 100}));
}

TEST(rdp_sampler_quad_without_filter_selects_plane_before_conversion) {
    Sampler sample;
    sample.modes = 1ULL << 45U;
    sample.memory[0] = 10;
    sample.memory[13] = 100;
    CHECK_EQ(sample.sample(8, 8)[0], 10);
    CHECK_EQ(sample.sample(24, 24)[0], 100);
}

TEST(rdp_sampler_convert_one_converts_previous_sample_without_memory) {
    Sampler sample;
    sample.convert = {127, 448, 448, 255, 0, 0};
    sample.modes = 1ULL << 41U;
    CHECK_EQ(sample.sample(0, 0, 1, {10, 20, 100, 255}), (RdpColor{120, 85, 120, 100}));
}

TEST(rdp_sampler_convert_one_filtered_mode_uses_previous_coefficients) {
    Sampler sample;
    sample.modes = (1ULL << 41U) | (1ULL << 42U) | (1ULL << 45U);
    CHECK_EQ(sample.sample(8, 8, 1, {128, 64, 100, 255})[0], 164);
    CHECK_EQ(sample.sample(24, 24, 1, {128, 64, 100, 255})[0], 0);
    sample.modes |= 1ULL << 44U;
    CHECK_EQ(sample.sample(16, 16, 1, {128, 64, 100, 255})[0], -60);
    sample.modes &= ~(1ULL << 45U);
    CHECK_EQ(sample.sample(8, 8, 1, {128, 64, 100, 255}), (RdpColor{100, 100, 100, 100}));
}
