#include "texture_commands.hpp"

using namespace cupid;
using namespace test::rdp;

TEST(rdp_texture_rectangle_samples_rgba32_through_color_pipeline) {
    TextureCommands commands;
    commands.textured();
    commands.run();
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 4; ++x)
            CHECK_EQ(commands.pixel(x, y), TextureCommands::expected(x, y));
    CHECK_EQ(commands.pixel(4), 0U);
    CHECK_EQ(commands.pixel(0, 2), 0U);
}

TEST(rdp_texture_rectangle_flip_exchanges_derivative_axes) {
    TextureCommands commands;
    commands.textured(0, 0, 12, 8, 0, 0, 1024, 1024, true);
    commands.run();
    CHECK_EQ(commands.pixel(0, 0), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(1, 0), TextureCommands::expected(0, 1));
    CHECK_EQ(commands.pixel(2, 0), TextureCommands::expected(0, 2));
    CHECK_EQ(commands.pixel(0, 1), TextureCommands::expected(1, 0));
}

TEST(rdp_texture_rectangle_fractional_derivatives_step_every_pixel) {
    TextureCommands commands;
    commands.textured(0, 0, 16, 4, 0, 0, 512, 0);
    commands.run();
    CHECK_EQ(commands.pixel(0), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(2), TextureCommands::expected(1, 0));
    CHECK_EQ(commands.pixel(3), TextureCommands::expected(1, 0));
}

TEST(rdp_texture_rectangle_signed_derivatives_and_lower_clamp) {
    TextureCommands commands;
    commands.textured(0, 0, 16, 4, 64, 0, -1024, 0);
    commands.run();
    CHECK_EQ(commands.pixel(0), TextureCommands::expected(2, 0));
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(1, 0));
    CHECK_EQ(commands.pixel(2), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(3), TextureCommands::expected(0, 0));
}

TEST(rdp_texture_rectangle_accumulators_wrap_before_signed_extraction) {
    TextureCommands commands;
    commands.textured(0, 0, 8, 4, 32760, 0);
    commands.run();
    CHECK_EQ(commands.pixel(0), TextureCommands::expected(15, 0));
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(0, 0));
}

TEST(rdp_texture_rectangle_fractional_origin_corrects_s_but_keeps_row_origin) {
    TextureCommands commands;
    commands.modes(1ULL << 3U);
    commands.textured(1, 1, 12, 8);
    commands.run();
    CHECK_EQ(commands.pixel(0, 0), TextureCommands::expected(0, 0, 4));
    CHECK_EQ(commands.pixel(1, 0), TextureCommands::expected(0, 0, 5));
    CHECK_EQ(commands.pixel(2, 0), TextureCommands::expected(1, 0, 5));
    CHECK_EQ(commands.pixel(1, 1), TextureCommands::expected(0, 1));
}

TEST(rdp_texture_rectangle_scissor_preserves_unclipped_texture_origin) {
    TextureCommands commands;
    commands.append(0x2d, (4ULL << 44U) | (4ULL << 32U) | (12ULL << 12U) | 8U);
    commands.textured(0, 0, 16, 12);
    commands.run();
    CHECK_EQ(commands.pixel(0, 1), 0U);
    CHECK_EQ(commands.pixel(1, 1), TextureCommands::expected(1, 1));
    CHECK_EQ(commands.pixel(2, 1), TextureCommands::expected(2, 1));
    CHECK_EQ(commands.pixel(3, 1), 0U);
}

TEST(rdp_texture_rectangle_field_selection_keeps_texture_row_steps) {
    TextureCommands commands;
    commands.append(0x2d, (3ULL << 24U) | (64ULL << 12U) | 16U);
    commands.textured(0, 0, 4, 16);
    commands.run();
    CHECK_EQ(commands.pixel(0, 0), 0U);
    CHECK_EQ(commands.pixel(0, 1), TextureCommands::expected(0, 1));
    CHECK_EQ(commands.pixel(0, 2), 0U);
    CHECK_EQ(commands.pixel(0, 3), TextureCommands::expected(0, 3));
}

TEST(rdp_texture_rectangle_filters_fractional_samples) {
    TextureCommands commands;
    commands.modes(1ULL << 45U);
    commands.textured(0, 0, 4, 4, 16, 16, 0, 0);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x0c3040e0U);
}

TEST(rdp_texture_rectangle_alpha_compare_uses_sample_alpha) {
    TextureCommands commands;
    commands.system->bus.memory.write(0x10004, 4, (TextureCommands::source_color(1, 0) & 0xffffff00U) | 129U);
    commands.append(0x39, 129);
    commands.modes(1);
    commands.textured(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0U);
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(1, 0));
}

TEST(rdp_texture_rectangle_two_cycle_uses_neighbor_tile) {
    TextureCommands commands;
    commands.second_tile();
    commands.modes(1ULL << 52U);
    commands.append(0x3c, combine_word({.d = 2, .ad = 2}, {.d = 0, .ad = 0}));
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 2));
}

TEST(rdp_texture_rectangle_second_cycle_texel_zero_is_second_sample) {
    TextureCommands commands;
    commands.second_tile();
    commands.modes(1ULL << 52U);
    commands.append(0x3c, combine_word({.d = 1, .ad = 1}, {.d = 1, .ad = 1}));
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 2));
}

TEST(rdp_texture_rectangle_one_cycle_texel_one_reads_next_pixel) {
    TextureCommands commands;
    commands.append(0x3c, combine_word({}, {.d = 2, .ad = 2}));
    commands.textured(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), TextureCommands::expected(1, 0));
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(2, 0));
}

TEST(rdp_texture_rectangle_texel_one_long_span_peeks_next_row) {
    TextureCommands commands;
    commands.append(0x3c, combine_word({}, {.d = 2, .ad = 2}));
    commands.textured(0, 0, 33, 8);
    commands.run();
    CHECK_EQ(commands.pixel(7, 0), TextureCommands::expected(8, 0));
    CHECK_EQ(commands.pixel(8, 0), TextureCommands::expected(0, 1, 1));
    CHECK_EQ(commands.pixel(8, 1), TextureCommands::expected(9, 1, 1));
}

TEST(rdp_texture_rectangle_texel_one_short_span_does_not_peek_next_row) {
    TextureCommands commands;
    commands.append(0x3c, combine_word({}, {.d = 2, .ad = 2}));
    commands.textured(0, 0, 29, 8);
    commands.run();
    CHECK_EQ(commands.pixel(7, 0), TextureCommands::expected(8, 0, 1));
}

TEST(rdp_texture_rectangle_convert_one_uses_first_sample) {
    TextureCommands commands;
    commands.append(0x2f, (15ULL << 36U) | (1ULL << 52U) | (1ULL << 41U) | (1ULL << 43U));
    commands.append(0x3c, combine_word({.d = 1, .ad = 1}, {.d = 1, .ad = 1}));
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x404040e0U);
}

TEST(rdp_texture_rectangle_lod_limits_both_samples_to_base_tile) {
    TextureCommands commands;
    commands.second_tile();
    commands.modes((1ULL << 52U) | (1ULL << 48U));
    commands.append(0x3c, combine_word({.d = 2, .ad = 2}, {.d = 0, .ad = 0}));
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 0));
}

TEST(rdp_texture_rectangle_detail_lod_selects_next_tile) {
    TextureCommands commands;
    commands.second_tile();
    commands.modes((1ULL << 52U) | (1ULL << 48U) | (1ULL << 50U));
    commands.append(0x3c, combine_word({.d = 1, .ad = 1}, {.d = 0, .ad = 0}));
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 2));
}

TEST(rdp_texture_rectangle_lod_fraction_reaches_combiner_without_tile_lod) {
    TextureCommands commands;
    commands.modes(1ULL << 52U);
    commands.append(0x3c, combine_word({.a = 6, .c = 13, .d = 7}, {.d = 0}));
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0xffffffe0U);
}

TEST(rdp_texture_rectangle_perspective_zero_w_saturates_before_tile_clamp) {
    TextureCommands commands;
    commands.modes(1ULL << 51U);
    commands.textured(0, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(15, 3));
}

TEST(rdp_texture_rectangle_fill_rectangle_can_select_tmem) {
    TextureCommands commands;
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 0));
}

TEST(rdp_texture_rectangle_waits_for_complete_attribute_word) {
    TextureCommands commands;
    commands.textured(0, 0, 4, 4);
    const u32 complete = commands.end;
    commands.system->bus.rdp.write_register(0, 0x1000);
    commands.system->bus.rdp.write_register(4, complete - 8U);
    CHECK_EQ(commands.pixel(), 0U);
    commands.system->bus.rdp.write_register(4, complete);
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 0));
}

TEST(rdp_texture_rectangle_rgba16_load_expands_into_rgba32_framebuffer) {
    TextureCommands commands;
    commands.system->bus.memory.write(0x11000, 4, 0xf80107c1);
    commands.append(0x3d, (2ULL << 51U) | (1ULL << 32U) | 0x11000U);
    commands.append(0x35, (2ULL << 51U) | (1ULL << 41U));
    commands.append(0x34, 4ULL << 12U);
    commands.textured(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0xff0000e0U);
    CHECK_EQ(commands.pixel(1), 0x00ff00e0U);
}

TEST(rdp_texture_rectangle_ia8_load_supplies_intensity_and_alpha) {
    TextureCommands commands;
    commands.system->bus.memory.write(0x11000, 2, 0xabcd);
    commands.append(0x3d, (1ULL << 51U) | (1ULL << 32U) | 0x11000U);
    commands.append(0x35, (3ULL << 53U) | (1ULL << 51U) | (1ULL << 41U));
    commands.append(0x34, 4ULL << 12U);
    commands.append(0x39, 192);
    commands.modes(1);
    commands.textured(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0U);
    CHECK_EQ(commands.pixel(1), 0xcccccce0U);
}

TEST(rdp_texture_rectangle_ci4_load_and_tlut_select_palette_colors) {
    TextureCommands commands;
    commands.system->bus.memory.write(0x11000, 2, 0x1230);
    commands.append(0x3d, (1ULL << 51U) | (1ULL << 32U) | 0x11000U);
    commands.append(0x35, (2ULL << 53U) | (1ULL << 51U) | (1ULL << 41U));
    commands.append(0x34, 4ULL << 12U);
    commands.append(0x35, (2ULL << 53U) | (1ULL << 41U) | (5ULL << 20U));
    commands.append(0x32, 12ULL << 12U);
    commands.system->bus.memory.write(0x12000, 8, 0x0001f80107c1003fULL);
    commands.append(0x3d, (2ULL << 51U) | (3ULL << 32U) | 0x12000U);
    commands.append(0x35, (336ULL << 32U) | (7ULL << 24U));
    commands.append(0x30, (7ULL << 24U) | (12ULL << 12U));
    commands.modes(1ULL << 47U);
    commands.textured(0, 0, 16, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0xff0000e0U);
    CHECK_EQ(commands.pixel(1), 0x00ff00e0U);
    CHECK_EQ(commands.pixel(2), 0x0000ffe0U);
    CHECK_EQ(commands.pixel(3), 0x000000e0U);
}

TEST(rdp_texture_rectangle_yuv_load_uses_programmed_conversion_factors) {
    TextureCommands commands;
    commands.system->bus.memory.write(0x11000, 4, 0x8a649478);
    commands.append(0x3d, (2ULL << 51U) | (1ULL << 32U) | 0x11000U);
    commands.append(0x35, (1ULL << 53U) | (2ULL << 51U) | (1ULL << 41U));
    commands.append(0x34, 4ULL << 12U);
    commands.append(0x2c, (127ULL << 45U) | (448ULL << 36U) | (448ULL << 27U) | (255ULL << 18U));
    commands.append(0x2f, 15ULL << 36U);
    commands.textured(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0x785578e0U);
    CHECK_EQ(commands.pixel(1), 0x8c698ce0U);
}
