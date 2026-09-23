#include "triangle_commands.hpp"

using namespace cupid;
using namespace test::rdp;

TEST(rdp_color_triangles_all_opcodes_use_primitive_combiner_in_both_cycles) {
    for (unsigned opcode = 8; opcode < 16; ++opcode) {
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            TriangleCommands commands;
            commands.modes(static_cast<u64>(cycle) << 52U);
            commands.triangle(opcode);
            commands.run();
            for (unsigned y = 0; y < 3; ++y)
                for (unsigned x = 0; x < 5; ++x)
                    CHECK_EQ(commands.pixel(x, y), x < 4U && y < 2U ? 0x804020e0U : 0U);
        }
    }
}

TEST(rdp_color_triangle_shade_packet_supplies_all_four_channels) {
    TriangleCommands commands;
    commands.shade.value = {0x00120000, 0x00340000, 0x00560000, 0x00800000};
    commands.append(0x3c, combine_word({}, {.d = 4, .ad = 4}));
    commands.modes((1ULL << 13U) | (1ULL << 12U));
    commands.triangle(12);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x12345660U);
}

TEST(rdp_color_triangle_texture_packet_reaches_sampler) {
    TriangleCommands commands;
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.triangle(10);
    commands.run();
    CHECK_EQ(commands.pixel(0, 0), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(3, 1), TextureCommands::expected(3, 1));
}

TEST(rdp_color_triangle_depth_packet_follows_optional_attribute_blocks) {
    for (unsigned opcode : {9U, 11U, 13U, 15U}) {
        TriangleCommands commands;
        commands.modes(1ULL << 5U);
        commands.z = {0x70000000, 0x00010000, 0, 0};
        commands.triangle(opcode);
        commands.run();
        CHECK_EQ(commands.depth(0), 0x6000U);
        CHECK_EQ(commands.depth(1), 0x6004U);
        CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9000), 1U);
    }
}

TEST(rdp_color_triangle_primitive_depth_overrides_packet_and_derivatives) {
    TriangleCommands commands;
    commands.append(0x2e, 0x40000020);
    commands.z = {0x70000000, 0x00010000, 0x00020000, 0x00030000};
    commands.modes((1ULL << 5U) | 4U);
    commands.triangle(15);
    commands.run();
    CHECK_EQ(commands.depth(0), 0x2001U);
    CHECK_EQ(commands.depth(3, 1), 0x2001U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9000), 1U);
}

TEST(rdp_color_triangle_eight_sample_diagonal_coverage) {
    TriangleCommands commands;
    commands.modes(1ULL << 3U);
    commands.triangle(8, {.middle = 16, .bottom = 16, .upper_step = -0x10000});
    commands.run();
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            const u32 expected = x + y < 3U ? 0x804020e0U : x + y == 3U ? 0x80402060U : 0U;
            CHECK_EQ(commands.pixel(x, y), expected);
        }
    }
}

TEST(rdp_color_triangle_shared_edges_accumulate_exact_coverage) {
    TriangleCommands commands;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            commands.system->bus.memory.write(commands.address(x, y), 4, 0xe0);
    commands.modes((1ULL << 3U) | (1ULL << 6U));
    commands.triangle(8, {.middle = 16, .bottom = 16, .upper_step = -0x10000});
    commands.triangle(
        8, {.middle = 16, .bottom = 16, .major = 0x40000, .upper_step = -0x10000, .left_major = false});
    commands.run();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            CHECK_EQ(commands.pixel(x, y), 0x804020e0U);
}

TEST(rdp_color_triangle_shade_centroid_uses_first_covered_sample) {
    TriangleCommands commands;
    commands.shade.value = {0x00400000, 0x00200000, 0x00100000, 0x00ff0000};
    commands.shade.dx[0] = 0x00200000;
    commands.shade.dy[0] = 0x00400000;
    commands.append(0x3c, combine_word({}, {.d = 4, .ad = 4}));
    commands.modes(1ULL << 3U);
    commands.triangle(12, {.top = 1, .middle = 4, .bottom = 4});
    commands.run();
    CHECK_EQ(commands.pixel(), 0x582010a0U);
}

TEST(rdp_color_triangle_fractional_top_without_aa_rejects_missing_first_sample) {
    TriangleCommands commands;
    commands.triangle(8, {.top = 1, .middle = 7, .bottom = 7});
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.pixel(0, 1), 0x804020a0U);
}

TEST(rdp_color_triangle_negative_origin_preserves_attribute_row_interpolation) {
    TriangleCommands commands;
    commands.shade.de[0] = 0x00010000;
    commands.append(0x3c, combine_word({}, {.d = 4, .ad = 4}));
    commands.triangle(12, {.top = -4, .middle = 4, .bottom = 4, .major = -0x10000, .upper = 0x20000});
    commands.run();
    CHECK_EQ(commands.pixel(), 0x814020e0U);
    CHECK_EQ(commands.pixel(2), 0U);
}

TEST(rdp_color_triangle_blend_color_uses_rgba_order) {
    TriangleCommands commands;
    commands.append(0x39, 0x123456ff);
    commands.modes(2ULL << 30U);
    commands.triangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x123456e0U);
}

TEST(rdp_color_triangle_shade_texture_modulation_uses_both_attribute_blocks) {
    for (bool two_cycles : {false, true}) {
        TriangleCommands commands;
        const CombineCycle modulation{.a = 1, .c = 4, .d = 7, .aa = 1, .ac = 4, .ad = 7};
        commands.append(0x3c, two_cycles ? combine_word(modulation, {.d = 0, .ad = 0})
                                         : combine_word({}, modulation));
        commands.modes(two_cycles ? 1ULL << 52U : 0U);
        commands.triangle(14);
        commands.run();
        CHECK_EQ(commands.pixel(), 0x040808e0U);
        CHECK_EQ(commands.pixel(3, 1), 0x101008e0U);
    }
}

TEST(rdp_color_triangle_shade_alpha_reaches_blender) {
    TriangleCommands commands;
    commands.system->bus.memory.write(commands.address(0), 4, 0x204060e0);
    commands.shade.value[3] = 128U << 16U;
    commands.modes((1ULL << 14U) | (1ULL << 22U) | (2ULL << 26U));
    commands.triangle(12);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x504040e0U);
}

TEST(rdp_color_triangle_shade_fraction_words_retain_subinteger_steps) {
    TriangleCommands commands;
    commands.shade.value = {0x00408000, 0x00114000, 0x0004c000, 0x00ff0000};
    commands.shade.dx[0] = 0x00018000;
    commands.append(0x3c, combine_word({}, {.d = 4, .ad = 4}));
    commands.triangle(12);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x401104e0U);
    CHECK_EQ(commands.pixel(1), 0x421104e0U);
    CHECK_EQ(commands.pixel(2), 0x431104e0U);
}

TEST(rdp_color_triangle_fractional_major_edge_corrects_shade_origin) {
    TriangleCommands commands;
    commands.shade.dx[0] = 32U << 16U;
    commands.append(0x3c, combine_word({}, {.d = 4, .ad = 4}));
    commands.modes(1ULL << 3U);
    commands.triangle(12, {.major = 0x4000});
    commands.run();
    CHECK_EQ(commands.pixel(), 0x884020a0U);
}

TEST(rdp_color_triangle_right_major_attributes_keep_unclipped_origin) {
    TriangleCommands commands;
    commands.shade.dx[0] = 32U << 16U;
    commands.append(0x3c, combine_word({}, {.d = 4, .ad = 4}));
    commands.append(0x2d, (8ULL << 44U) | (32ULL << 12U) | 8U);
    commands.triangle(12, {.major = 0x40000, .upper = 0, .left_major = false});
    commands.run();
    CHECK_EQ(commands.pixel(0), 0U);
    CHECK_EQ(commands.pixel(1), 0U);
    CHECK_EQ(commands.pixel(2), 0x404020e0U);
    CHECK_EQ(commands.pixel(3), 0x604020e0U);
}

TEST(rdp_color_triangle_depth_overlap_rejects_farther_and_accepts_nearer) {
    TriangleCommands commands;
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 4; ++x)
            commands.system->bus.memory.write(0x9000U + (y * 16U + x) * 2U, 2, 0xfffc);
    commands.modes((1ULL << 4U) | (1ULL << 5U));
    commands.triangle(9);
    commands.append(0x3a, 0x00ff00ff);
    commands.z[0] = 0x60000000;
    commands.triangle(9);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
    commands.end = 0x1000;
    commands.append(0x3a, 0x0000ffff);
    commands.z[0] = 0x20000000;
    commands.triangle(9);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x0000ffe0U);
    CHECK_EQ(commands.depth(0), 0x1000U);
}

TEST(rdp_color_triangle_depth_compare_without_update_still_uses_interpolated_depth) {
    TriangleCommands commands;
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 4; ++x)
            commands.system->bus.memory.write(0x9000U + (y * 16U + x) * 2U, 2, 0x2000);
    commands.modes(1ULL << 4U);
    commands.z[0] = 0x60000000;
    commands.triangle(9);
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.depth(0), 0x2000U);
}

TEST(rdp_color_triangle_without_depth_test_or_update_preserves_delta_for_blending) {
    TriangleCommands commands;
    commands.append(0x3a, 0xa0a0a080U);
    commands.system->bus.memory.write(commands.address(0), 4, 0x404040e0U);
    commands.system->bus.memory.write(0x9000U, 2, 0x2468U);
    commands.modes((1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U));
    commands.z = {0x40000000U, 0x7fff0000U, 0, 0};
    commands.triangle(9);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x909090e0U);
    CHECK_EQ(commands.depth(0), 0x2468U);
}

TEST(rdp_color_triangle_rgba16_packs_color_and_hidden_coverage) {
    TriangleCommands commands;
    commands.append(0x3f, (2ULL << 51U) | (15ULL << 32U) | 0x8000U);
    commands.triangle(15);
    commands.run();
    CHECK_EQ(commands.system->bus.memory.read(0x8000, 2), 0x8209U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), 3U);
}

TEST(rdp_color_triangle_perspective_interpolates_w) {
    TriangleCommands commands;
    commands.texture.value = {32U << 16U, 0, 0x40000000, 0};
    commands.texture.dx[2] = 0x20000000;
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes(1ULL << 51U);
    commands.triangle(10);
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(2, 0));
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(2, 0));
}

TEST(rdp_color_triangle_lod_uses_packet_maximum_level_and_vertical_gradient) {
    for (bool vertical : {false, true}) {
        TriangleCommands commands;
        commands.tile(1, 32);
        commands.append(0x32, (1ULL << 24U) | (60ULL << 12U));
        commands.texture.dx = {vertical ? 0U : 64U << 16U, 0, 0, 0};
        commands.texture.dy = {0, vertical ? 64U << 16U : 0U, 0, 0};
        commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
        commands.modes(1ULL << 48U);
        commands.triangle(10, {.maximum_level = 3});
        commands.run();
        // The new tile starts on an odd source row but addresses it as row zero.
        CHECK_EQ(commands.pixel(), TextureCommands::expected(2, 1));
    }
}

TEST(rdp_color_triangle_lod_retains_perspective_precision_before_tile_clamp) {
    TriangleCommands commands;
    commands.texture.value = {16000U << 16U, 0, 0x40000000, 0};
    commands.texture.dx = {1000U << 16U, 0, 0, 0};
    commands.texture.dy = {};
    commands.append(0x3c, combine_word({.a = 6, .c = 13, .d = 7}, {.d = 0}));
    commands.modes((1ULL << 51U) | (1ULL << 52U));
    commands.triangle(10, {.maximum_level = 7});
    commands.run();
    CHECK_EQ(commands.pixel(), 0xf4f4f4e0U);
}

TEST(rdp_color_triangle_perspective_overflow_forces_distant_lod) {
    TriangleCommands commands;
    commands.tile(3, 96);
    commands.append(0x32, (3ULL << 24U) | (60ULL << 12U));
    commands.texture.value = {20000U << 16U, 0, 0x20000000, 0};
    commands.texture.dx = {};
    commands.texture.dy = {};
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes((1ULL << 51U) | (1ULL << 48U));
    commands.triangle(10, {.maximum_level = 3});
    commands.run();
    CHECK_EQ(commands.pixel(), TextureCommands::expected(13, 3));
}

TEST(rdp_color_triangles_wait_for_complete_packets_for_every_opcode) {
    for (unsigned opcode = 8; opcode < 16; ++opcode) {
        TriangleCommands commands;
        commands.triangle(opcode);
        const u32 complete = commands.end;
        commands.system->bus.rdp.write_register(0, 0x1000);
        commands.system->bus.rdp.write_register(4, complete - 8U);
        CHECK_EQ(commands.pixel(), 0U);
        commands.system->bus.rdp.write_register(4, complete);
        CHECK_EQ(commands.pixel(), 0x804020e0U);
    }
}

TEST(rdp_color_triangle_texel1_looks_ahead_to_valid_long_span) {
    for (bool field : {false, true}) {
        for (bool long_span : {false, true}) {
            TriangleCommands commands;
            commands.append(0x3c, combine_word({}, {.d = 2, .ad = 2}));
            commands.modes(8U);
            commands.append(0x2d, (static_cast<u64>(field) << 25U) | (64ULL << 12U) | 16U);
            commands.triangle(10, {.upper = long_span ? 0x98000 : 0x78000});
            commands.run();
            const unsigned x = long_span ? 9U : 7U;
            CHECK_EQ(commands.pixel(x - 1U), TextureCommands::expected(x, 0));
            CHECK_EQ(commands.pixel(x), long_span && !field ? TextureCommands::expected(0, 1, 3)
                                                            : TextureCommands::expected(x + 1U, 0, 3));
            CHECK_EQ(commands.pixel(0, 1), field ? 0U : TextureCommands::expected(1, 1));
        }
    }
}

TEST(rdp_color_triangle_texel1_steps_left_for_right_major_spans) {
    TriangleCommands commands;
    commands.texture.value[0] = 4U * 32U << 16U;
    commands.append(0x3c, combine_word({}, {.d = 2, .ad = 2}));
    commands.triangle(10, {.major = 0x40000, .upper = 0, .lower = 0, .left_major = false});
    commands.run();
    CHECK_EQ(commands.pixel(1), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(3), TextureCommands::expected(2, 0));
}

TEST(rdp_color_triangle_early_depth_rejection_preserves_framebuffer) {
    TriangleCommands commands;
    commands.modes((1ULL << 4U) | (1ULL << 5U));
    commands.system->bus.memory.write(0x9000U, 2, 0x2000U);
    commands.system->bus.memory.write(0x8000U, 4, 0x11223344U);
    commands.z = {0x70000000, 0, 0, 0};
    commands.triangle(9);
    commands.run();
    CHECK_EQ(commands.pixel(0, 0), 0x11223344U);
    CHECK_EQ(commands.depth(0), 0x2000U);
}

TEST(rdp_color_triangle_consecutive_horizontal_texture_point_matches_stepped) {
    TriangleCommands commands;
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes(1ULL << 48U);
    commands.triangle(10, {.major = 0, .upper = 0x60000, .lower = 0x60000});
    commands.run();
    for (unsigned x = 0; x < 6; ++x) {
        CHECK_EQ(commands.pixel(x, 0), TextureCommands::expected(x, 0));
        CHECK_EQ(commands.pixel(x, 1), TextureCommands::expected(x, 1));
    }
}
