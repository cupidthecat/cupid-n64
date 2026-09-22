#include "rdp/copy_commands.hpp"

#include <array>

namespace {
using namespace cupid;

struct Texture {
    std::array<u32, 3> value{};
    std::array<u32, 3> dx{128U << 16U, 0, 0};
    std::array<u32, 3> de{0, 32U << 16U, 0};
    std::array<u32, 3> dy{0, 32U << 16U, 0};
};

struct Geometry {
    s32 top = 0, middle = 8, bottom = 8;
    s32 major = 0, upper = 7 << 16, lower = 0;
    s32 major_step = 0, upper_step = 0, lower_step = 0;
    bool left_major = true;
    unsigned opcode = 10, tile = 0;
};

struct CopyTriangle : test::rdp::CopyCommands {
    using CopyCommands::CopyCommands;

    void triangle(Texture attributes = {}, Geometry g = {}) {
        append(g.opcode, (static_cast<u64>(g.left_major) << 55U) | (static_cast<u64>(g.tile) << 48U) |
                             (static_cast<u64>(static_cast<u32>(g.bottom) & 0x3fffU) << 32U) |
                             (static_cast<u64>(static_cast<u32>(g.middle) & 0x3fffU) << 16U) |
                             (static_cast<u32>(g.top) & 0x3fffU));
        for (const auto& edge :
             {std::array<s32, 2>{g.lower, g.lower_step}, {g.major, g.major_step}, {g.upper, g.upper_step}})
            append(0, (static_cast<u64>(static_cast<u32>(edge[0])) << 32U) | static_cast<u32>(edge[1]));
        if ((g.opcode & 4U) != 0)
            for (unsigned i = 0; i < 8; ++i)
                append(0, 0xabcdef9876543210ULL);
        if ((g.opcode & 2U) != 0) {
            std::array<u32, 16> words{};
            const std::array vectors{attributes.value, attributes.dx, attributes.de, attributes.dy};
            for (unsigned i = 0; i < vectors.size(); ++i) {
                const auto& v = vectors[i];
                const unsigned index = (i / 2U) * 8U + (i & 1U) * 2U;
                words[index] = (v[0] & 0xffff0000U) | (v[1] >> 16U);
                words[index + 1] = (v[2] & 0xffff0000U) | 0xfaceU;
                words[index + 4] = (v[0] << 16U) | (v[1] & 0xffffU);
                words[index + 5] = (v[2] << 16U) | 0xbeefU;
            }
            for (unsigned i = 0; i < words.size(); i += 2)
                append(0, (static_cast<u64>(words[i]) << 32U) | words[i + 1]);
        }
        if ((g.opcode & 1U) != 0) {
            append(0, 0xfedcba9876543210ULL);
            append(0, 0x0123456789abcdefULL);
        }
    }
};
} // namespace

TEST(rdp_copy_triangle_interpolates_texture_rows_and_four_pixel_groups) {
    CopyTriangle c;
    c.triangle();
    c.run();
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 16; ++x)
            CHECK_EQ(c.pixel(x, y), y < 2 && x < 8 ? 0x1001ULL + (y * 16U + x) * 2U : 0ULL);
}

TEST(rdp_copy_triangle_right_major_reverses_groups_and_lane_order) {
    CopyTriangle c;
    Geometry g;
    g.left_major = false;
    g.major = 7 << 16;
    g.upper = 0;
    Texture t;
    t.value[0] = 128U << 16U;
    c.triangle(t, g);
    c.run();
    constexpr std::array<unsigned, 8> columns{3, 2, 1, 0, 7, 6, 5, 4};
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK_EQ(c.pixel(x, y), 0x1001ULL + (y * 16U + columns[x]) * 2U);
}

TEST(rdp_copy_triangle_clipped_span_restarts_groups_and_advances_row_attributes) {
    CopyTriangle c;
    c.scissor(9, 4, 25, 8);
    c.triangle();
    c.run();
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 16; ++x)
            CHECK_EQ(c.pixel(x, y), y == 1 && x >= 2 && x <= 6 ? 0x1021ULL + (x - 2U) * 2U : 0ULL);
}

TEST(rdp_copy_triangle_fractional_top_preserves_whole_row_attribute_origin) {
    CopyTriangle c;
    Geometry g;
    g.top = 3;
    g.bottom = 5;
    c.triangle({}, g);
    c.run();
    CHECK_EQ(c.pixel(0, 0), 0x1001ULL);
    CHECK_EQ(c.pixel(0, 1), 0x1021ULL);
    CHECK_EQ(c.pixel(0, 2), 0ULL);
}

TEST(rdp_copy_triangle_offset_latch_uses_edge_minus_vertical_derivative) {
    CopyTriangle c;
    Geometry g;
    g.major = 1 << 16;
    g.major_step = -0x10000;
    Texture t;
    t.de[0] = 128U << 16U;
    c.triangle(t, g);
    c.run();
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK_EQ(c.pixel(x, y), 0x1001ULL + (3U + x + y * 20U) * 2U);
}

TEST(rdp_copy_triangle_negative_horizontal_derivative) {
    CopyTriangle c;
    Texture t;
    t.value[0] = 128U << 16U;
    t.dx[0] = static_cast<u32>(-128) << 16U;
    c.triangle(t);
    c.run();
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + (x < 4 ? x + 4U : x - 4U) * 2U);
}

TEST(rdp_copy_triangle_uses_command_tile_and_ignores_shade_and_depth) {
    for (unsigned opcode = 8; opcode < 16; ++opcode) {
        CopyTriangle c;
        c.tile(2, 0, 5, 16);
        Geometry g;
        g.tile = 5;
        g.opcode = opcode;
        c.triangle({}, g);
        c.run();
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                const unsigned index = (opcode & 2U) != 0 ? y * 16U + x : x & 3U;
                CHECK_EQ(c.pixel(x, y), 0x1081ULL + index * 2U);
            }
    }
}

TEST(rdp_copy_triangle_eight_bit_destination_uses_eight_pixel_groups) {
    CopyTriangle c(1);
    Geometry g;
    g.upper = 15 << 16;
    c.triangle({}, g);
    c.run();
    for (unsigned x = 0; x < 16; ++x)
        CHECK_EQ(c.pixel(x), (x & 1U) == 0 ? 0x10ULL : x);
}

TEST(rdp_copy_triangle_alpha_compare_preserves_rejected_pixel_and_hidden_bits) {
    CopyTriangle c;
    c.system->bus.memory.write(0x10002, 2, 0x1234);
    c.system->bus.memory.write(0x8002, 2, 0xabcd);
    c.system->bus.memory.set_hidden_pair(0x8002, 2);
    c.modes(1);
    c.triangle();
    c.run();
    CHECK_EQ(c.pixel(0), 0x1001ULL);
    CHECK_EQ(c.pixel(1), 0xabcdULL);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8002), 2U);
}

TEST(rdp_copy_triangle_perspective_divides_coordinates_and_horizontal_derivative) {
    CopyTriangle c;
    c.modes(1ULL << 51U);
    Texture t;
    t.value = {32U << 16U, 0, 0x40000000U};
    t.dx[0] = 64U << 16U;
    t.de[1] = t.dy[1] = 16U << 16U;
    c.triangle(t);
    c.run();
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK_EQ(c.pixel(x, y), 0x1001ULL + (2U + x + y * 16U) * 2U);
}

TEST(rdp_copy_triangle_nonpositive_w_saturates_both_coordinates) {
    for (u32 w : {0U, 0x80000000U, 0xffff0000U}) {
        CopyTriangle c;
        c.modes(1ULL << 51U);
        c.tile(2, (2ULL << 14U) | (2ULL << 4U));
        Texture t;
        t.value = {0xffff0000U, 0xffff0000U, w};
        c.triangle(t);
        c.run();
        for (unsigned x = 0; x < 8; ++x)
            CHECK_EQ(c.pixel(x), 0x1061ULL + ((3U + x) & 3U) * 2U);
    }
}

TEST(rdp_copy_triangle_waits_for_complete_texture_and_depth_attributes) {
    CopyTriangle c;
    Geometry g;
    g.opcode = 15;
    c.triangle({}, g);
    c.system->bus.rdp.write_register(0, 0x1000);
    c.system->bus.rdp.write_register(4, c.end - 8U);
    CHECK_EQ(c.pixel(0), 0ULL);
    c.system->bus.rdp.write_register(4, c.end);
    CHECK_EQ(c.pixel(0), 0x1001ULL);
}

TEST(rdp_copy_triangle_perspective_interpolates_w_across_groups_and_rows) {
    CopyTriangle c;
    c.modes(1ULL << 51U);
    Texture t;
    t.value = {128U << 16U, 0, 0x40000000U};
    t.dx = {0, 0, 0x20000000U};
    t.de[2] = t.dy[2] = 0x10000000U;
    c.triangle(t);
    c.run();
    for (unsigned x = 0; x < 8; ++x) {
        CHECK_EQ(c.pixel(x, 0), 0x1001ULL + ((x < 4 ? 8U : 5U) + (x & 3U)) * 2U);
        CHECK_EQ(c.pixel(x, 1), 0x1021ULL + ((x < 4 ? 6U : 4U) + (x & 3U)) * 2U);
    }
}

TEST(rdp_copy_triangle_negative_perspective_coordinates_reach_tile_masks) {
    CopyTriangle c;
    c.modes(1ULL << 51U);
    c.tile(2, (2ULL << 14U) | (2ULL << 4U));
    Texture t;
    t.value = {0xffe00000U, 0xffe00000U, 0x40000000U};
    t.dx = {};
    c.triangle(t);
    c.run();
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1041ULL + ((2U + x) & 3U) * 2U);
}

TEST(rdp_copy_triangle_coordinates_wrap_before_signed_extraction) {
    CopyTriangle c;
    c.tile(2, 2ULL << 4U);
    Texture t;
    t.value[0] = 0x7fff0000U;
    t.dx[0] = 32U << 16U;
    c.triangle(t);
    c.run();
    constexpr std::array<unsigned, 8> columns{3, 0, 1, 2, 0, 1, 2, 3};
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + columns[x] * 2U);
}

TEST(rdp_copy_triangle_quantizes_row_base_and_horizontal_derivative) {
    for (u32 derivative : {0x3ffU, 0x400U}) {
        CopyTriangle c;
        Texture t;
        t.value[0] = 0x001fffffU;
        t.dx[0] = derivative;
        c.triangle(t);
        c.run();
        for (unsigned x = 0; x < 8; ++x) {
            const unsigned offset = x >= 4 && derivative == 0x400 ? 1U : 0U;
            CHECK_EQ(c.pixel(x), 0x1001ULL + ((x & 3U) + offset) * 2U);
        }
    }
}

TEST(rdp_copy_triangle_fractional_x_does_not_adjust_texture_origin) {
    CopyTriangle c;
    Geometry g;
    g.major = 0xc000;
    g.upper = 0x7c000;
    c.triangle({}, g);
    c.run();
    for (unsigned x = 0; x < 8; ++x)
        CHECK_EQ(c.pixel(x), 0x1001ULL + x * 2U);
}

TEST(rdp_copy_triangle_field_selection_preserves_absolute_row_interpolation) {
    for (unsigned field : {2U, 3U}) {
        CopyTriangle c;
        c.scissor(0, 0, 64, 16, field);
        c.triangle();
        c.run();
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 8; ++x)
                CHECK_EQ(c.pixel(x, y), y == (field & 1U) ? 0x1001ULL + (y * 16U + x) * 2U : 0ULL);
    }
}

TEST(rdp_copy_triangle_sloped_spans_restart_texture_groups_each_row) {
    CopyTriangle c;
    Geometry g;
    g.bottom = g.middle = 16;
    g.upper = 7 << 16;
    g.upper_step = -0x20000;
    c.triangle({}, g);
    c.run();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 16; ++x)
            CHECK_EQ(c.pixel(x, y), x <= 7U - y * 2U ? 0x1001ULL + (y * 16U + x) * 2U : 0ULL);
}

TEST(rdp_copy_triangle_four_bit_destination_writes_zero_bytes) {
    CopyTriangle c(0);
    c.system->bus.memory.write(0x8000, 8, 0xffffffffffffffffULL);
    c.triangle();
    c.run();
    CHECK_EQ(c.system->bus.memory.read(0x8000, 8), 0ULL);
    CHECK_EQ(c.system->bus.memory.hidden_pair(0x8000), 0U);
}
