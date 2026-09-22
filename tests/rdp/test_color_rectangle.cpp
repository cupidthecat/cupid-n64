#include "color_commands.hpp"

using namespace cupid;
using namespace test::rdp;

TEST(rdp_color_rectangle_primitive_rgba32_and_exclusive_edges) {
    ColorCommands commands;
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
    CHECK_EQ(commands.pixel(1), 0U);
    CHECK_EQ(commands.pixel(0, 1), 0U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0)), 0U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0) + 2), 0U);
}

TEST(rdp_color_rectangle_rgba16_stores_split_coverage) {
    ColorCommands commands(2);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x8209U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0)), 3U);
}

TEST(rdp_color_rectangle_environment_and_command_order) {
    ColorCommands commands;
    commands.append(0x3c, combine_word({}, {.d = 5, .ad = 5}));
    commands.append(0x3b, 0x123456ff);
    commands.rectangle();
    commands.append(0x3b, 0xabcdef80);
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x123456e0U);
    CHECK_EQ(commands.pixel(1), 0xabcdefe0U);
}

TEST(rdp_color_rectangle_two_cycle_primitive_environment_interpolation) {
    ColorCommands commands;
    commands.modes(1ULL << 52U);
    commands.append(0x3a, (128ULL << 32U) | 0xc08040ffU);
    commands.append(0x3b, 0x402000ff);
    commands.append(0x3c, combine_word({}, {.a = 0, .b = 5, .c = 14, .d = 5}));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x805020e0U);
}

TEST(rdp_color_rectangle_key_and_convert_command_fields) {
    ColorCommands commands;
    commands.append(0x3a, 0xc0a080ff);
    commands.append(0x2b, (0xabcULL << 16U) | (64U << 8U) | 128U);
    commands.append(0x2a, (0xdefULL << 44U) | (0x123ULL << 32U) | (32ULL << 24U) | (64ULL << 16U) | 32U);
    commands.append(0x3c, combine_word({}, {.a = 3, .b = 6, .c = 6, .d = 7}));
    commands.rectangle();
    commands.append(0x2c, (511ULL << 9U) | 128U);
    commands.append(0x3c, combine_word({}, {.a = 8, .b = 7, .c = 15, .d = 7}));
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x402010e0U);
    CHECK_EQ(commands.pixel(1), 0x010101e0U);
}

TEST(rdp_color_rectangle_key_alpha_compare_rejects_before_depth_update_and_accepts_equality) {
    ColorCommands commands;
    commands.append(0x3e, 0x9000);
    commands.system->bus.memory.write(0x9000, 2, 0xdead);
    commands.system->bus.memory.set_hidden_pair(0x9000, 2);
    commands.append(0x2e, (0x4000ULL << 16U) | 0x20U);
    commands.append(0x39, 1);
    commands.append(0x3a, 0x814020ff);
    commands.append(0x2a, (32ULL << 44U) | (32ULL << 32U) | (64ULL << 24U) | (255ULL << 16U) | (32ULL << 8U) |
                              255U);
    commands.append(0x2b, (16ULL << 16U) | (128ULL << 8U) | 255U);
    commands.append(0x3c, combine_word({}, {.a = 3, .b = 6, .c = 6, .d = 7}));
    commands.modes((1ULL << 40U) | (1ULL << 5U) | (1ULL << 2U) | 1U);
    commands.rectangle();
    commands.append(0x2b, (24ULL << 16U) | (128ULL << 8U) | 255U);
    commands.rectangle(4, 0, 8, 4);
    commands.run();

    CHECK_EQ(commands.pixel(0), 0U);
    CHECK_EQ(commands.system->bus.memory.read(0x9000, 2), 0xdeadU);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9000), 2U);
    CHECK_EQ(commands.pixel(1), 0x814020e0U);
    CHECK_EQ(commands.system->bus.memory.read(0x9002, 2), 0x2001U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9002), 1U);
}

TEST(rdp_color_rectangle_key_alpha_drives_blender_weight) {
    ColorCommands commands;
    commands.system->bus.memory.write(commands.address(0), 4, 0x204060e0);
    commands.append(0x3a, 0x804020ff);
    commands.append(0x2a, (16ULL << 44U) | (16ULL << 32U) | (64ULL << 24U) | (255ULL << 16U) | (32ULL << 8U) |
                              255U);
    commands.append(0x2b, (16ULL << 16U) | (128ULL << 8U) | 255U);
    commands.append(0x3c, combine_word({}, {.a = 3, .b = 6, .c = 6, .d = 7}));
    commands.modes((1ULL << 40U) | (1ULL << 14U) | (1ULL << 22U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x504040e0U);
}

TEST(rdp_color_rectangle_two_cycle_key_compare_uses_first_cycle_key_alpha) {
    ColorCommands commands;
    commands.append(0x3a, 0x818080ff);
    commands.append(0x3b, 0x808080ff);
    commands.append(0x39, 1);
    commands.append(0x2a, (32ULL << 44U) | (32ULL << 32U) | (128ULL << 24U) | (255ULL << 16U) |
                              (128ULL << 8U) | 255U);
    commands.append(0x2b, (16ULL << 16U) | (128ULL << 8U) | 255U);
    commands.append(0x3c, combine_word({.a = 3, .b = 6, .c = 6, .d = 7}, {.a = 5, .b = 6, .c = 6, .d = 7}));
    commands.modes((1ULL << 52U) | (1ULL << 40U) | 1U);
    commands.rectangle();
    commands.append(0x39, 0);
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0U);
    CHECK_EQ(commands.pixel(1), 0x808080e0U);
}

TEST(rdp_color_rectangle_blender_can_select_fog_or_blend_color) {
    ColorCommands commands;
    commands.append(0x38, 0x12345678);
    commands.append(0x39, 0xabcdefab);
    commands.modes(3ULL << 30U);
    commands.rectangle();
    commands.modes(2ULL << 30U);
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x123456e0U);
    CHECK_EQ(commands.pixel(1), 0xabcdefe0U);
}

TEST(rdp_color_rectangle_alpha_compare_accepts_equality) {
    ColorCommands commands;
    commands.modes(1);
    commands.append(0x39, 128);
    commands.append(0x3a, 0x8040207f);
    commands.rectangle();
    commands.append(0x3a, 0x80402080);
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.pixel(1), 0x804020e0U);
}

TEST(rdp_color_rectangle_two_cycle_alpha_test_uses_first_alpha) {
    ColorCommands commands;
    commands.modes((1ULL << 52U) | 1U);
    commands.append(0x39, 128);
    commands.append(0x3a, 0x8040207f);
    commands.append(0x3b, 0xffffffff);
    commands.append(0x3c, combine_word({}, {.d = 5, .ad = 5}));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
}

TEST(rdp_color_rectangle_force_blends_framebuffer_rgb) {
    ColorCommands commands;
    commands.system->bus.memory.write(commands.address(0), 4, 0x204060e0);
    commands.append(0x3a, 0x80402080);
    commands.modes((1ULL << 14U) | (1ULL << 22U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x504040e0U);
}

TEST(rdp_color_rectangle_framebuffer_rgba16_reads_truncated_channels) {
    ColorCommands commands(2);
    commands.system->bus.memory.write(commands.address(0), 2, 0xffff);
    commands.append(0x3a, 0x01010180);
    commands.modes((1ULL << 14U) | (1ULL << 22U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x7bdfU);
}

TEST(rdp_color_rectangle_antialias_uses_eight_staggered_samples) {
    ColorCommands commands;
    commands.modes(1ULL << 3U);
    commands.rectangle(1, 0, 4, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020a0U);
}

TEST(rdp_color_rectangle_without_antialias_requires_first_sample) {
    ColorCommands commands;
    commands.rectangle(1, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.pixel(1), 0x804020e0U);
}

TEST(rdp_color_rectangle_partial_rows_and_exclusive_bottom) {
    ColorCommands commands;
    commands.modes(1ULL << 3U);
    commands.rectangle(0, 1, 4, 3);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x80402060U);
    CHECK_EQ(commands.pixel(0, 1), 0U);
}

TEST(rdp_color_rectangle_scissor_clips_subpixels) {
    ColorCommands commands;
    commands.modes(1ULL << 3U);
    commands.append(0x2d, (1ULL << 44U) | (1ULL << 32U) | (4ULL << 12U) | 3U);
    commands.rectangle(0, 0, 8, 8);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x80402040U);
    CHECK_EQ(commands.pixel(1), 0U);
    CHECK_EQ(commands.pixel(0, 1), 0U);
}

TEST(rdp_color_rectangle_field_selects_odd_rows) {
    ColorCommands commands;
    commands.append(0x2d, (3ULL << 24U) | (32ULL << 12U) | 16U);
    commands.rectangle(0, 0, 4, 16);
    commands.run();
    CHECK_EQ(commands.pixel(0, 0), 0U);
    CHECK_EQ(commands.pixel(0, 1), 0x804020e0U);
    CHECK_EQ(commands.pixel(0, 2), 0U);
    CHECK_EQ(commands.pixel(0, 3), 0x804020e0U);
}

TEST(rdp_color_rectangle_coverage_modes_read_stored_coverage) {
    ColorCommands commands;
    for (unsigned mode = 0; mode < 4; ++mode) {
        commands.system->bus.memory.write(commands.address(mode), 4, 0x00000040);
        commands.modes((static_cast<u64>(mode) << 8U) | (1ULL << 6U) | (1ULL << 3U));
        commands.rectangle(mode * 4U, 0, mode * 4U + 4U, 2);
    }
    commands.run();
    CHECK_EQ(commands.pixel(0), 0x804020c0U);
    CHECK_EQ(commands.pixel(1), 0x804020c0U);
    CHECK_EQ(commands.pixel(2), 0x804020e0U);
    CHECK_EQ(commands.pixel(3), 0x80402040U);
}

TEST(rdp_color_rectangle_coverage_save_defaults_to_full_without_image_read) {
    ColorCommands commands;
    commands.modes(3ULL << 8U);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
}

TEST(rdp_color_rectangle_alpha_modulates_coverage) {
    ColorCommands commands;
    commands.append(0x3a, 0x80402080);
    commands.modes((1ULL << 3U) | (1ULL << 12U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x80402060U);
}

TEST(rdp_color_rectangle_alpha_zero_rejects_modulated_antialias_pixel) {
    ColorCommands commands;
    commands.append(0x3a, 0x80402000);
    commands.modes((1ULL << 3U) | (1ULL << 12U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
}

TEST(rdp_color_rectangle_color_on_coverage_preserves_memory_until_wrap) {
    ColorCommands commands;
    commands.system->bus.memory.write(commands.address(0), 4, 0x12345620);
    commands.system->bus.memory.write(commands.address(1), 4, 0x123456e0);
    commands.modes((1ULL << 3U) | (1ULL << 6U) | (1ULL << 7U) | (1ULL << 22U));
    commands.rectangle(0, 0, 8, 2);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0x123456a0U);
    CHECK_EQ(commands.pixel(1), 0x80402060U);
}

TEST(rdp_color_rectangle_magic_dither_thresholds) {
    ColorCommands commands;
    commands.append(0x3a, 0x090b0fff);
    commands.append(0x2f, 3ULL << 36U);
    commands.rectangle(0, 0, 16, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0x101010e0U);
    CHECK_EQ(commands.pixel(1), 0x090b10e0U);
    CHECK_EQ(commands.pixel(2), 0x091010e0U);
    CHECK_EQ(commands.pixel(3), 0x090b0fe0U);
}

TEST(rdp_color_rectangle_alpha_dither_changes_comparison_threshold) {
    ColorCommands commands;
    commands.append(0x3a, 0x8040207e);
    commands.append(0x39, 128);
    commands.append(0x2f, 1U);
    commands.rectangle(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0U);
    CHECK_EQ(commands.pixel(1), 0x804020e0U);
}

TEST(rdp_color_rectangle_primitive_depth_delta_controls_memory_blend_shift) {
    ColorCommands commands;
    commands.append(0x3a, 0xa0a0a080);
    commands.modes((1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U) | 4U);
    commands.system->bus.memory.write(commands.address(0), 4, 0x404040e0);
    commands.system->bus.memory.write(commands.address(1), 4, 0x404040e0);
    commands.append(0x2e, 1);
    commands.rectangle();
    commands.append(0x2e, 0xffff8000U);
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0x585858e0U);
    CHECK_EQ(commands.pixel(1), 0x909090e0U);
}

TEST(rdp_color_rectangle_reset_clears_color_constants_and_mux) {
    ColorCommands commands;
    commands.run();
    commands.system->bus.rdp.reset();
    commands.end = 0x1000;
    commands.append(0x3f, (3ULL << 51U) | (7ULL << 32U) | 0x8000U);
    commands.modes();
    commands.append(0x3c, combine_word({}, {}));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0xe0U);
}

TEST(rdp_color_rectangle_depth_delta_encoding_combines_bit_positions) {
    ColorCommands commands;
    commands.append(0x3a, 0xa0a0a080);
    commands.modes((1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U) | 4U);
    commands.system->bus.memory.write(commands.address(0), 4, 0x404040e0);
    commands.append(0x2e, 0x1800);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x909090e0U);
}

TEST(rdp_color_rectangle_rgba32_hidden_bits_follow_written_halfwords) {
    ColorCommands commands;
    commands.append(0x3a, 0x081103ff);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x081103e0U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0)), 3U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0) + 2U), 0U);
}

TEST(rdp_color_rectangle_antialias_blends_with_memory_coverage) {
    ColorCommands commands;
    commands.system->bus.memory.write(commands.address(0), 4, 0x20406040);
    commands.append(0x3a, 0x80402080);
    commands.modes((1ULL << 6U) | (1ULL << 3U) | (1ULL << 22U));
    commands.rectangle(0, 0, 4, 2);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x504040c0U);
}
