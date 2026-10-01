#include "triangle_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {

void check_bank_timing_equal(const TriangleCommands& first, const TriangleCommands& second) {
    const auto& a = first.system->bus.memory;
    const auto& b = second.system->bus.memory;
    CHECK_EQ(a.bank_status(), b.bank_status());
    CHECK_EQ(a.errors(), b.errors());
    CHECK_EQ(a.clock(), b.clock());
    for (u32 bank = 0; bank < 8; ++bank) {
        const u32 base = bank << 20U;
        CHECK_EQ(a.bank_access_clock(base), b.bank_access_clock(base));
        for (u32 row = 0; row < 512; ++row)
            CHECK_EQ(a.row_open(base + (row << 11U)), b.row_open(base + (row << 11U)));
    }
}

void configure_texel0_perspective(TriangleCommands& commands) {
    commands.system->bus.rdp.set_parallel_rasterization(false);
    commands.texture.value = {32U << 16U, 0, 0x40000000U, 0};
    commands.texture.dx = {};
    commands.texture.de = {};
    commands.texture.dy = {};
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes(1ULL << 51U);
}

void seed_clear_depth(TriangleCommands& commands) {
    for (unsigned y = 0; y < 2; ++y) {
        for (unsigned x = 0; x < 16; ++x)
            commands.system->bus.memory.write_halfword(0x9000U + (y * 16U + x) * 2U, {0xfffcU, 3U});
    }
}

} // namespace

TEST(rdp_triangle_texture_needs_no_lod_ignores_vertical_neighbor_overflow_without_changing_bank_timing) {
    TriangleCommands baseline;
    TriangleCommands unused_neighbor;
    configure_texel0_perspective(baseline);
    configure_texel0_perspective(unused_neighbor);

    // This makes the unused next-Y W equal zero. Without LOD, only the current
    // texture point participates in the draw.
    unused_neighbor.texture.dy[2] = 0xc0000000U;
    const TriangleGeometry one_pixel{.middle = 4, .bottom = 4, .upper = 0x10000, .lower = 0x10000};
    baseline.triangle(10, one_pixel);
    unused_neighbor.triangle(10, one_pixel);
    baseline.run();
    unused_neighbor.run();

    check_bank_timing_equal(baseline, unused_neighbor);
    CHECK_EQ(baseline.pixel(), TextureCommands::expected(2, 0));
    CHECK_EQ(unused_neighbor.pixel(), TextureCommands::expected(2, 0));
}

TEST(rdp_triangle_texture_needs_lod_includes_vertical_neighbor_overflow) {
    TriangleCommands commands;
    commands.system->bus.rdp.set_parallel_rasterization(false);
    commands.tile(3, 96);
    commands.append(0x32, (3ULL << 24U) | (60ULL << 12U));
    commands.texture.value = {0, 0, 0x40000000U, 0};
    commands.texture.dx = {};
    commands.texture.de = {};
    commands.texture.dy = {0, 0, 0xc0000000U, 0};
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes((1ULL << 51U) | (1ULL << 48U));
    commands.triangle(10, {.middle = 4, .bottom = 4, .upper = 0x10000, .lower = 0x10000, .maximum_level = 3});
    commands.run();

    // Current and next-X have positive W. Only next-Y overflows, so selecting
    // the farthest LOD proves that its overflow still joins the LOD decision.
    // Loaded row three has its RGBA32 halfwords swapped in TMEM. A tile base
    // of 96 with local T=0 therefore starts at source column two.
    CHECK_EQ(commands.pixel(), TextureCommands::expected(2, 3));
}

TEST(rdp_triangle_texture_needs_one_cycle_texel1_uses_neighbor_and_next_row_boundary) {
    TriangleCommands commands;
    commands.system->bus.rdp.set_parallel_rasterization(false);
    commands.texture.value[0] = 2U * 32U << 16U;
    commands.append(0x3c, combine_word({}, {.d = 2, .ad = 2}));
    commands.modes(8U);
    commands.triangle(10, {.upper = 0x98000});
    commands.run();

    CHECK_EQ(commands.pixel(8), TextureCommands::expected(11, 0));
    CHECK_EQ(commands.pixel(9), TextureCommands::expected(2, 1, 3));
    CHECK_EQ(commands.pixel(0, 1), TextureCommands::expected(3, 1));
}

TEST(rdp_triangle_texture_needs_two_cycle_texel1_uses_current_point_without_neighbor_overflow) {
    TriangleCommands commands;
    commands.system->bus.rdp.set_parallel_rasterization(false);
    commands.second_tile();
    commands.texture.value = {0, 0, 0x40000000U, 0};
    commands.texture.dx = {0, 0, 0xc0000000U, 0};
    commands.texture.de = {};
    commands.texture.dy = {};
    // The second combiner cycle exchanges texel0 and texel1.
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes((1ULL << 51U) | (1ULL << 52U));
    commands.triangle(10, {.middle = 4, .bottom = 4, .upper = 0x10000, .lower = 0x10000});
    commands.run();

    // The next-X W reaches zero, but two-cycle texel1 samples the second tile
    // at the current point and does not consume that neighbor.
    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 2));
}

TEST(rdp_triangle_texture_needs_dead_direct_terms_match_untextured_rgba_depth_and_coverage) {
    TriangleCommands baseline;
    TriangleCommands dead_textures;
    for (auto* commands : {&baseline, &dead_textures}) {
        commands->system->bus.rdp.set_parallel_rasterization(false);
        seed_clear_depth(*commands);
        commands->append(0x3a, 0x806040ffU);
        commands->modes((1ULL << 51U) | (1ULL << 6U) | (1ULL << 5U) | (1ULL << 4U) | (1ULL << 3U));
    }
    baseline.append(0x3c, combine_word({}, {.d = 3, .ad = 3}));
    dead_textures.append(
        0x3c, combine_word({}, {.a = 1, .b = 2, .c = 16, .d = 3, .aa = 1, .ab = 2, .ac = 7, .ad = 3}));
    baseline.triangle(0x0f);
    dead_textures.triangle(0x0f);
    baseline.run();
    dead_textures.run();

    CHECK(baseline.pixel() != 0U);
    for (unsigned y = 0; y < 2; ++y) {
        for (unsigned x = 0; x < 16; ++x) {
            CHECK_EQ(dead_textures.pixel(x, y), baseline.pixel(x, y));
            CHECK_EQ(dead_textures.depth(x, y), baseline.depth(x, y));
            const u32 color_address = baseline.address(x, y);
            const u32 depth_address = 0x9000U + (y * 16U + x) * 2U;
            CHECK_EQ(dead_textures.system->bus.memory.hidden_pair(color_address),
                     baseline.system->bus.memory.hidden_pair(color_address));
            CHECK_EQ(dead_textures.system->bus.memory.hidden_pair(depth_address),
                     baseline.system->bus.memory.hidden_pair(depth_address));
        }
    }
}

TEST(rdp_triangle_texture_needs_keyed_direct_bypass_keeps_texel0_live) {
    TriangleCommands commands;
    commands.system->bus.rdp.set_parallel_rasterization(false);
    commands.append(0x3b, 0xe0c0a0ffU);
    commands.append(0x3c, combine_word({}, {.a = 1, .b = 1, .c = 16, .d = 5, .ad = 3}));
    commands.modes(1ULL << 40U);
    commands.triangle(0x0f);
    commands.run();

    CHECK_EQ(commands.pixel(), TextureCommands::expected(0, 0));
}
