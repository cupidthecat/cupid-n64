#include "color_commands.hpp"
#include "texture_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {
constexpr u64 z_compare = 1ULL << 4U;
constexpr u64 z_update = 1ULL << 5U;
constexpr u64 primitive_z = 1ULL << 2U;
constexpr u64 image_read = 1ULL << 6U;
constexpr u64 antialias = 1ULL << 3U;

struct DepthCommands : ColorCommands {
    explicit DepthCommands(unsigned pixel_size = 3) : ColorCommands(pixel_size) {
        append(0x3e, 0x9000);
    }
    void depth(unsigned z, unsigned delta = 1) {
        append(0x2e, (static_cast<u64>(z) << 16U) | delta);
    }
    void seed_depth(unsigned x, u16 word, u8 hidden = 0, unsigned y = 0) {
        system->bus.memory.write(0x9000U + (y * 8U + x) * 2U, 2, word);
        system->bus.memory.set_hidden_pair(0x9000U + (y * 8U + x) * 2U, hidden);
    }
    u16 stored(unsigned x = 0, unsigned y = 0) const {
        return static_cast<u16>(system->bus.memory.read(0x9000U + (y * 8U + x) * 2U, 2));
    }
    u8 hidden(unsigned x = 0, unsigned y = 0) const {
        return system->bus.memory.hidden_pair(0x9000U + (y * 8U + x) * 2U);
    }
};
} // namespace

TEST(rdp_depth_rectangle_updates_compressed_depth_and_split_delta) {
    DepthCommands commands;
    commands.depth(0x4000, 0x20);
    commands.modes(primitive_z | z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
    CHECK_EQ(commands.stored(), 0x2001U);
    CHECK_EQ(commands.hidden(), 1U);
    CHECK_EQ(commands.stored(1), 0U);
}

TEST(rdp_depth_rectangle_rejects_farther_opaque_surface_without_writes) {
    DepthCommands commands;
    commands.seed_depth(0, 0x2001, 1);
    commands.depth(0x5000, 4);
    commands.modes(primitive_z | z_compare | z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.stored(), 0x2001U);
    CHECK_EQ(commands.hidden(), 1U);
}

TEST(rdp_depth_rectangle_equal_full_coverage_fails_opaque_compare) {
    DepthCommands commands;
    commands.seed_depth(0, 0x2000);
    commands.depth(0x4000);
    commands.modes(primitive_z | z_compare);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
}

TEST(rdp_depth_rectangle_partial_coverage_accepts_nearby_opaque_surface) {
    DepthCommands commands;
    commands.seed_depth(0, 0x2000);
    commands.depth(0x4001);
    commands.modes(primitive_z | z_compare | image_read | antialias);
    commands.rectangle(0, 0, 4, 2);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x80402080U);
}

TEST(rdp_depth_rectangle_transparent_mode_rejects_equal_partial_surface) {
    DepthCommands commands;
    commands.seed_depth(0, 0x2000);
    commands.depth(0x4000);
    commands.modes(primitive_z | z_compare | image_read | antialias | (2ULL << 10U));
    commands.rectangle(0, 0, 4, 2);
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
}

TEST(rdp_depth_rectangle_decal_passes_equal_depth_and_rejects_clear_depth) {
    DepthCommands commands;
    commands.seed_depth(0, 0x2000);
    commands.seed_depth(1, 0xfffc);
    commands.depth(0x4000);
    commands.modes(primitive_z | z_compare | (3ULL << 10U));
    commands.rectangle(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
    CHECK_EQ(commands.pixel(1), 0U);
}

TEST(rdp_depth_rectangle_interpenetration_scales_coverage) {
    DepthCommands commands;
    commands.seed_depth(0, 0x6000);
    commands.depth(0x6fff, 8);
    commands.modes(primitive_z | z_compare | (1ULL << 10U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x80402000U);
}

TEST(rdp_depth_rectangle_primitive_depth_ignores_high_bit) {
    DepthCommands commands;
    commands.depth(0xc000, 0x1800);
    commands.modes(primitive_z | z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.stored(), 0x2003U);
    CHECK_EQ(commands.hidden(), 3U);
}

TEST(rdp_depth_rectangle_interpolated_source_uses_zero_depth_and_unit_delta) {
    DepthCommands commands;
    commands.seed_depth(0, 0xffff, 3);
    commands.depth(0x7000, 0x8000);
    commands.modes(z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.stored(), 0U);
    CHECK_EQ(commands.hidden(), 0U);
}

TEST(rdp_depth_rectangle_disabled_update_preserves_depth_and_hidden_bits) {
    DepthCommands commands;
    commands.seed_depth(0, 0x8003, 2);
    commands.depth(0x4000);
    commands.modes(primitive_z | z_compare);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
    CHECK_EQ(commands.stored(), 0x8003U);
    CHECK_EQ(commands.hidden(), 2U);
}

TEST(rdp_depth_rectangle_alpha_rejection_preserves_depth) {
    DepthCommands commands;
    commands.seed_depth(0, 0xfffc, 3);
    commands.depth(0x4000);
    commands.append(0x39, 0xff);
    commands.append(0x3a, 0x80402080);
    commands.modes(primitive_z | z_compare | z_update | 1U);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.stored(), 0xfffcU);
    CHECK_EQ(commands.hidden(), 3U);
}

TEST(rdp_depth_rectangle_rgba16_overlap_uses_stored_depth) {
    DepthCommands commands(2);
    commands.seed_depth(0, 0xfffc);
    commands.depth(0x4000);
    commands.modes(primitive_z | z_compare | z_update);
    commands.rectangle();
    commands.depth(0x6000);
    commands.append(0x3a, 0x00ffffff);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x8209U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0)), 3U);
    CHECK_EQ(commands.stored(), 0x2000U);
    CHECK_EQ(commands.hidden(), 0U);
}

TEST(rdp_depth_rectangle_write_follows_color_when_buffers_alias) {
    DepthCommands commands(2);
    commands.append(0x3e, 0x8000);
    commands.depth(0x4000, 0x8000);
    commands.modes(primitive_z | z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x2003U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(0)), 3U);
}

TEST(rdp_depth_rectangle_cpu_overwrite_changes_hidden_delta_and_comparison) {
    DepthCommands commands;
    commands.seed_depth(0, 0x6000, 3);
    commands.seed_depth(1, 0x6000, 3);
    commands.system->bus.write(0x9002, 2, 0x6000);
    CHECK_EQ(commands.hidden(1), 0U);
    commands.depth(0x7002);
    commands.modes(primitive_z | z_compare | antialias | image_read);
    commands.rectangle(0, 0, 8, 2);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x80402080U);
    CHECK_EQ(commands.pixel(1), 0U);
}

TEST(rdp_depth_rectangle_sp_dma_overwrite_changes_hidden_delta) {
    DepthCommands commands;
    for (unsigned x = 0; x < 4; ++x)
        commands.seed_depth(x, 0x6000, 3);
    commands.system->bus.write(0x04000000, 8, 0x6000600160006001ULL);
    commands.system->rsp.write_register(0, 0);
    commands.system->rsp.write_register(4, 0x9000);
    commands.system->rsp.write_register(0x0c, 7);
    commands.system->rsp.tick(6);
    CHECK_EQ(commands.hidden(), 0U);
    CHECK_EQ(commands.hidden(1), 3U);
    commands.depth(0x7002);
    commands.modes(primitive_z | z_compare | antialias | image_read);
    commands.rectangle(0, 0, 8, 2);
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.pixel(1), 0x80402080U);
}

TEST(rdp_depth_rectangle_stored_delta_changes_pixel_alpha_shift) {
    DepthCommands commands;
    commands.seed_depth(0, 0x6001, 0);
    commands.seed_depth(1, 0x6001, 3);
    for (unsigned x = 0; x < 2; ++x)
        commands.system->bus.memory.write(commands.address(x), 4, 0x404040e0);
    commands.append(0x3a, 0xa0a0a080);
    commands.depth(0, 0x80);
    commands.modes(primitive_z | z_compare | (1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U));
    commands.rectangle(0, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x404040e0U);
    CHECK_EQ(commands.pixel(1), 0x909090e0U);
}

TEST(rdp_depth_textured_rectangles_compare_and_update_in_both_cycles_and_directions) {
    for (bool two_cycles : {false, true}) {
        for (bool flipped : {false, true}) {
            TextureCommands commands;
            commands.append(0x3e, 0x9000);
            commands.system->bus.memory.write(0x9000, 2, 0xfffc);
            commands.append(0x2e, 0x40000020);
            commands.append(0x3c, combine_word({}, {.d = two_cycles ? 2U : 1U, .ad = two_cycles ? 2U : 1U}));
            commands.modes(primitive_z | z_compare | z_update | (two_cycles ? 1ULL << 52U : 0U));
            commands.textured(0, 0, 8, 4, 32, 64, 1024, 1024, flipped);
            commands.run();
            CHECK_EQ(commands.pixel(), TextureCommands::expected(1, 2));
            CHECK_EQ(commands.pixel(1), 0U);
            CHECK_EQ(commands.system->bus.memory.read(0x9000, 2), 0x2001U);
            CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9000), 1U);
            CHECK_EQ(commands.system->bus.memory.read(0x9002, 2), 0U);
        }
    }
}

TEST(rdp_depth_rectangle_zero_alpha_coverage_preserves_depth) {
    DepthCommands commands;
    commands.seed_depth(0, 0x4321, 2);
    commands.depth(0x4000);
    commands.append(0x3a, 0x80402000);
    commands.modes(primitive_z | z_update | antialias | (1ULL << 12U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0U);
    CHECK_EQ(commands.stored(), 0x4321U);
    CHECK_EQ(commands.hidden(), 2U);
}

TEST(rdp_depth_rectangle_interpenetration_zero_coverage_obeys_antialias) {
    for (bool enabled : {false, true}) {
        DepthCommands commands;
        commands.seed_depth(0, 0x8004);
        commands.depth(0x7800, 8);
        commands.modes(primitive_z | z_compare | z_update | (1ULL << 10U) | (enabled ? antialias : 0U));
        commands.rectangle();
        commands.run();
        CHECK_EQ(commands.pixel(), enabled ? 0U : 0x804020e0U);
        CHECK_EQ(commands.stored(), enabled ? 0x8004U : 0x8000U);
        CHECK_EQ(commands.hidden(), enabled ? 0U : 3U);
    }
}

TEST(rdp_depth_rectangle_field_and_stride_match_color_coordinates) {
    for (unsigned size : {2U, 3U}) {
        DepthCommands commands(size);
        commands.depth(0x4000);
        commands.modes(primitive_z | z_update);
        commands.append(0x2d, (3ULL << 24U) | (32ULL << 12U) | 16U);
        commands.rectangle(4, 0, 8, 16);
        commands.run();
        for (unsigned y = 0; y < 4; ++y) {
            CHECK_EQ(commands.stored(0, y), 0U);
            CHECK_EQ(commands.stored(1, y), (y & 1U) != 0 ? 0x2000U : 0U);
            CHECK_EQ(commands.stored(2, y), 0U);
        }
    }
}

TEST(rdp_depth_rectangle_clear_then_draw_uses_fill_written_delta_bits) {
    DepthCommands commands;
    commands.append(0x3f, (2ULL << 51U) | (7ULL << 32U) | 0x9000U);
    commands.append(0x37, 0xfffcfffc);
    commands.modes(3ULL << 52U);
    commands.rectangle(0, 0, 4, 0);
    commands.append(0x3f, (3ULL << 51U) | (7ULL << 32U) | 0x8000U);
    commands.depth(0x6000, 0x40);
    commands.modes(primitive_z | z_compare | z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x804020e0U);
    CHECK_EQ(commands.stored(), 0x4001U);
    CHECK_EQ(commands.hidden(), 2U);
    CHECK_EQ(commands.stored(1), 0xfffcU);
}

TEST(rdp_depth_image_ignores_reserved_high_address_bits) {
    DepthCommands commands;
    commands.append(0x3e, 0xff009000);
    commands.depth(0x4000);
    commands.modes(primitive_z | z_update);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.stored(), 0x2000U);
}
