#include "rdp/copy_commands.hpp"

#include <array>

namespace {
using namespace cupid;
using test::rdp::CopyCommands;

} // namespace

TEST(rdp_copy_16bit_rectangle_reads_rows_and_inclusive_edges) {
    CopyCommands c;
    c.rectangle();
    c.run();
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 16; ++x)
            CHECK_EQ(c.pixel(x, y), y < 2 && x < 8 ? 0x1001ULL + (y * 16U + x) * 2U : 0ULL);
}

TEST(rdp_copy_interpolates_once_per_four_16bit_pixels) {
    CopyCommands c;
    c.rectangle(0, 0, 28, 0, 0, 0, 8192);
    c.run();
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + (x < 4 ? x : x + 4U) * 2U);
}

TEST(rdp_copy_left_scissor_restarts_copy_groups_without_advancing_s) {
    CopyCommands c;
    c.scissor(9, 4, 25, 8);
    c.rectangle(1, 1, 40, 8);
    c.run();
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 16; ++x)
            CHECK_EQ(c.pixel(x, y), y == 1 && x >= 2 && x <= 6 ? 0x1021ULL + (x - 2U) * 2U : 0ULL);
}

TEST(rdp_copy_flipped_rectangle_advances_t_between_groups_and_s_between_rows) {
    CopyCommands c;
    c.rectangle(0, 0, 28, 4, 0, 0, 1024, 4096, true);
    c.run();
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK_EQ(c.pixel(x, y), 0x1001ULL + (y + (x & 3U) + (x / 4U) * 64U) * 2U);
}

TEST(rdp_copy_alpha_compare_preserves_transparent_pixels_and_hidden_bits) {
    CopyCommands c;
    c.system->bus.memory.write(0x10002, 2, 0x2468);
    c.system->bus.memory.write(0x8002, 2, 0x7777);
    c.system->bus.memory.set_hidden_pair(0x8002, 2);
    c.modes(1);
    c.rectangle(0, 0, 12, 0);
    c.run();
    CHECK_EQ(c.pixel(0), 0x1001ULL);
    CHECK_EQ(c.pixel(1), 0x7777ULL);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8002), 2U);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8000), 3U);
}

TEST(rdp_copy_8bit_destination_uses_eight_pixel_groups_and_raw_halfwords) {
    CopyCommands c(1);
    c.rectangle(0, 0, 60, 0);
    c.run();
    for (unsigned x = 0; x < 16; ++x) {
        const unsigned texel = x / 2U;
        CHECK_EQ(c.pixel(x), (x & 1U) == 0 ? 0x10ULL : 1ULL + texel * 2U);
    }
}

TEST(rdp_copy_mask_mirror_applies_to_each_lane) {
    CopyCommands c;
    c.tile(2, (1ULL << 8U) | (2ULL << 4U));
    c.rectangle(0, 0, 28, 0, 64);
    c.run();
    constexpr std::array<unsigned, 8> indices{2, 3, 3, 2, 1, 0, 0, 1};
    for (unsigned x = 0; x < indices.size(); ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + indices[x] * 2U);
}

TEST(rdp_copy_ignores_clamp_and_subtracts_tile_origin_after_shift) {
    CopyCommands c;
    c.tile(2, (1ULL << 9U) | 1U);
    c.bounds(4, 0);
    c.rectangle(0, 0, 12, 0, 128);
    c.run();
    for (unsigned x = 0; x < 4; ++x)
        CHECK_EQ(c.pixel(x), 0x1003ULL + x * 2U);
}

TEST(rdp_copy_selects_tile_and_obeys_field_scissor) {
    CopyCommands c;
    c.tile(2, 0, 7, 4);
    c.scissor(0, 0, 64, 16, 3);
    c.rectangle(0, 0, 12, 12, 0, 0, 4096, 1024, false, 7);
    c.run();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            // Offsetting TMEM does not change the odd-row bank selection.
            const unsigned texel = (y + 1U) * 16U + (x ^ 2U);
            CHECK_EQ(c.pixel(x, y), (y & 1U) != 0 ? 0x1001ULL + texel * 2U : 0ULL);
        }
}

TEST(rdp_copy_non_rgba_16bit_framebuffer_does_not_write) {
    for (unsigned format = 1; format < 8; ++format) {
        CopyCommands c(2, format);
        c.rectangle();
        c.run();
        CHECK_EQ(c.pixel(0), 0ULL);
    }
}

TEST(rdp_copy_4bit_texture_replicates_only_first_two_halfwords) {
    CopyCommands c;
    c.system->bus.memory.write(0x10000, 2, 0x0123);
    c.tile(0);
    c.rectangle(0, 0, 12, 0);
    c.run();
    constexpr std::array<u16, 4> expected{0x0011, 0x2233, 0x0123, 0x0123};
    for (unsigned x = 0; x < expected.size(); ++x)
        CHECK_EQ(c.pixel(x), expected[x]);
}

TEST(rdp_copy_8bit_texture_replicates_only_first_two_halfwords) {
    CopyCommands c;
    c.tile(1);
    c.rectangle(0, 0, 12, 0);
    c.run();
    constexpr std::array<u16, 4> expected{0x1001, 0x1003, 0x1003, 0x1003};
    for (unsigned x = 0; x < expected.size(); ++x)
        CHECK_EQ(c.pixel(x), expected[x]);
}

TEST(rdp_copy_palette_4bit_uses_bank_and_raw_words_for_both_palette_types) {
    for (unsigned type = 0; type < 2; ++type) {
        CopyCommands c;
        c.system->bus.memory.write(0x10000, 2, 0x0123);
        for (unsigned i = 0; i < 256; ++i)
            c.system->bus.memory.write(0x20000U + i * 2U, 2, 0xa001U + i * 2U);
        c.append(0x3d, (2ULL << 51U) | (255ULL << 32U) | 0x20000U);
        c.tile(0, 0, 7, 256);
        c.append(0x30, (7ULL << 24U) | (1020ULL << 12U));
        c.tile(0, 1ULL << 20U);
        c.modes((1ULL << 47U) | (static_cast<u64>(type) << 46U));
        c.rectangle(0, 0, 12, 0);
        c.run();
        for (unsigned x = 0; x < 4; ++x)
            CHECK_EQ(c.pixel(x), 0xa021ULL + x * 2U);
    }
}

TEST(rdp_copy_palette_selects_one_bank_per_halfword_lane) {
    CopyCommands c;
    c.system->bus.memory.write(0x10000, 8, 0);
    c.append(0x3d, (2ULL << 51U) | (15ULL << 32U) | 0x10020U);
    c.tile(2, 0, 7, 256);
    c.append(0x34, (7ULL << 24U) | (12ULL << 12U));
    c.tile(1);
    c.modes(1ULL << 47U);
    c.rectangle(0, 0, 12, 0);
    c.run();
    for (unsigned x = 0; x < 4; ++x)
        CHECK_EQ(c.pixel(x), 0x1021ULL + x * 2U);
}

TEST(rdp_copy_signed_coordinates_wrap_through_tmem) {
    CopyCommands c;
    c.tile(2, 3ULL << 4U);
    c.rectangle(0, 0, 12, 0, -32);
    c.run();
    constexpr std::array<unsigned, 4> indices{7, 0, 1, 2};
    for (unsigned x = 0; x < indices.size(); ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + indices[x] * 2U);
}

TEST(rdp_copy_large_shift_wraps_at_sixteen_bits_before_tile_origin) {
    CopyCommands c;
    c.tile(2, (3ULL << 4U) | 15U);
    c.rectangle(0, 0, 12, 0, 0x4010);
    c.run();
    for (unsigned x = 0; x < 4; ++x)
        CHECK_EQ(c.pixel(x), 0x1003ULL + x * 2U);
}

TEST(rdp_copy_negative_derivative_changes_group_origin) {
    CopyCommands c;
    c.rectangle(0, 0, 28, 0, 128, 0, -4096);
    c.run();
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + (x < 4 ? x + 4U : x - 4U) * 2U);
}

TEST(rdp_copy_perspective_rectangle_saturates_zero_w) {
    CopyCommands c;
    c.tile(2, (2ULL << 14U) | (2ULL << 4U));
    c.modes(1ULL << 51U);
    c.rectangle(0, 0, 12, 0);
    c.run();
    for (unsigned x = 0; x < 4; ++x)
        CHECK_EQ(c.pixel(x), 0x1061ULL + ((3U + x) & 3U) * 2U);
}

TEST(rdp_copy_8bit_alpha_compare_is_ignored_and_even_byte_preserves_hidden_pair) {
    CopyCommands c(1);
    c.system->bus.memory.set_hidden_pair(0x8000, 2);
    c.modes(1);
    c.rectangle(0, 0, 0, 0);
    c.run();
    CHECK_EQ(c.pixel(0), 0x10ULL);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8000), 2U);
}

TEST(rdp_copy_4bit_destination_writes_zero_bytes_and_odd_hidden_pairs) {
    CopyCommands c(0);
    c.system->bus.memory.write(0x8000, 4, 0xffffffffU);
    c.rectangle(0, 0, 8, 0);
    c.run();
    CHECK_EQ(c.system->bus.memory.read(0x8000, 4), 0xffULL);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8000), 0U);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8002), 3U);
}

TEST(rdp_copy_invalid_and_clipped_rectangles_do_not_write) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        CopyCommands c;
        if (mode == 0)
            c.rectangle(7, 0, 4, 0);
        else if (mode == 1)
            c.rectangle(0, 3, 4, 0);
        else {
            c.scissor(0, 0, mode == 2 ? 0U : 64U, mode == 3 ? 0U : 64U);
            c.rectangle();
        }
        c.run();
        CHECK_EQ(c.pixel(0), 0ULL);
        CHECK_EQ(c.pixel(1), 0ULL);
    }
}

TEST(rdp_copy_waits_for_complete_rectangle_attributes) {
    CopyCommands c;
    c.rectangle(0, 0, 12, 0);
    c.system->bus.rdp.write_register(0, 0x1000);
    c.system->bus.rdp.write_register(4, c.end - 8U);
    CHECK_EQ(c.pixel(0), 0ULL);
    c.system->bus.rdp.write_register(4, c.end);
    CHECK_EQ(c.pixel(0), 0x1001ULL);
}

TEST(rdp_copy_fill_rectangle_uses_zero_texture_attributes) {
    CopyCommands c;
    c.rectangle(0, 0, 0, 0, 128, 128);
    c.append(0x36, (28ULL << 44U) | (7ULL << 24U));
    c.run();
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + (x & 3U) * 2U);
}

TEST(rdp_copy_textured_rectangle_in_fill_cycle_uses_fill_color) {
    for (bool flipped : {false, true}) {
        CopyCommands c;
        c.append(0x2f, 3ULL << 52U);
        c.append(0x37, 0x12345679U);
        c.rectangle(0, 0, 12, 0, 0, 0, 0, 0, flipped);
        c.run();
        for (unsigned x = 0; x < 4; ++x)
            CHECK_EQ(c.pixel(x), (x & 1U) == 0 ? 0x1234ULL : 0x5679ULL);
    }
}

TEST(rdp_copy_32bit_tile_reads_only_lower_bank_with_byte_replication) {
    CopyCommands c;
    c.tile(3, 0, 0, 256);
    c.rectangle(0, 0, 12, 0);
    c.run();
    constexpr std::array<u16, 4> expected{0x1010, 0x1010, 0x1005, 0x1007};
    for (unsigned x = 0; x < expected.size(); ++x)
        CHECK_EQ(c.pixel(x), expected[x]);
}

TEST(rdp_copy_raw_texture_format_does_not_convert_texels) {
    for (unsigned format = 0; format < 8; ++format) {
        CopyCommands c;
        c.tile(2, static_cast<u64>(format) << 53U);
        c.rectangle(0, 0, 12, 0);
        c.run();
        for (unsigned x = 0; x < 4; ++x)
            CHECK_EQ(c.pixel(x), 0x1001ULL + x * 2U);
    }
}

TEST(rdp_copy_t_shift_mirror_and_negative_rows) {
    CopyCommands c;
    c.tile(2, (1ULL << 18U) | (2ULL << 14U) | (1ULL << 10U));
    c.rectangle(0, 0, 0, 4, 0, -64, 0, 2048);
    c.run();
    CHECK_EQ(c.pixel(0, 0), 0x1001ULL);
    CHECK_EQ(c.pixel(0, 1), 0x1001ULL);
}

TEST(rdp_copy_mask_fields_above_ten_use_ten_bit_period) {
    for (unsigned mask = 10; mask < 16; ++mask) {
        CopyCommands c;
        c.tile(2, (1ULL << 8U) | (static_cast<u64>(mask) << 4U));
        c.rectangle(0, 0, 0, 0, -32);
        c.run();
        CHECK_EQ(c.pixel(0), 0x1001ULL);
    }
}

TEST(rdp_copy_fractional_derivative_accumulates_before_coordinate_extraction) {
    CopyCommands c;
    c.rectangle(0, 0, 60, 0, 0, 0, 512);
    c.run();
    for (unsigned x = 0; x < 16; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + ((x & 3U) + x / 8U) * 2U);
}

TEST(rdp_copy_8bit_odd_byte_updates_hidden_pair_from_low_bit) {
    CopyCommands c(1);
    c.rectangle(0, 0, 4, 0);
    c.run();
    CHECK_EQ(c.pixel(1), 1ULL);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8000), 3U);
}
