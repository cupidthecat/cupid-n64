#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

struct TriangleFill {
    std::unique_ptr<System> system = std::make_unique<System>();
    u32 end = 0x1000;
    unsigned size;
    static constexpr u32 color = 0x12355679;

    explicit TriangleFill(unsigned destination_size = 3, unsigned format = 0, unsigned width = 8)
        : size(destination_size) {
        test::initialize_memory(*system);
        word((0x3fULL << 56U) | (static_cast<u64>(format) << 53U) | (static_cast<u64>(size) << 51U) |
             (static_cast<u64>(width - 1U) << 32U) | 0x8000U);
        word((0x2fULL << 56U) | (3ULL << 52U));
        word((0x37ULL << 56U) | color);
    }
    void word(u64 value) {
        system->bus.memory.write(end, 8, value);
        end += 8;
    }
    void scissor(unsigned left, unsigned top, unsigned right, unsigned bottom, unsigned field = 0) {
        word((0x2dULL << 56U) | (static_cast<u64>(left) << 44U) | (static_cast<u64>(top) << 32U) |
             (static_cast<u64>(field) << 24U) | (static_cast<u64>(right) << 12U) | bottom);
    }
    void triangle(s32 yh = 0, s32 ym = 16, s32 yl = 16, s32 xh = 0x10000, s32 xm = 0x40000, s32 xl = 0,
                  s32 dh = 0, s32 dm = -0x10000, s32 dl = 0, bool left_major = true, unsigned opcode = 8) {
        word((static_cast<u64>(opcode) << 56U) | (static_cast<u64>(left_major) << 55U) |
             (static_cast<u64>(static_cast<u32>(yl) & 0x3fffU) << 32U) |
             (static_cast<u64>(static_cast<u32>(ym) & 0x3fffU) << 16U) | (static_cast<u32>(yh) & 0x3fffU));
        for (const auto& edge : {std::array<s32, 2>{xl, dl}, {xh, dh}, {xm, dm}})
            word((static_cast<u64>(static_cast<u32>(edge[0])) << 32U) | static_cast<u32>(edge[1]));
        const unsigned attribute_words =
            ((opcode & 4U) ? 8U : 0U) + ((opcode & 2U) ? 8U : 0U) + ((opcode & 1U) ? 2U : 0U);
        for (unsigned i = 0; i < attribute_words; ++i)
            word(0xfedcba9876543210ULL);
    }
    void run() {
        word(0x29ULL << 56U);
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
        CHECK_EQ(system->bus.rdp.current(), end);
        CHECK_EQ(system->bus.rdp.read_register(12) & 0x62U, 0U);
        CHECK_EQ(system->bus.read(0x04300008, 4) & 0x20U, 0x20U);
    }
    void rows(const std::array<unsigned, 5>& masks) {
        for (unsigned y = 0; y < masks.size(); ++y)
            for (unsigned x = 0; x < 8; ++x)
                CHECK_EQ(system->bus.memory.read(0x8000U + (y * 8U + x) * 4U, 4),
                         (masks[y] & (1U << x)) != 0 ? static_cast<u64>(color) : 0ULL);
    }
};
} // namespace

TEST(rdp_fill_triangle_left_major_uses_inclusive_spans) {
    TriangleFill c;
    c.triangle();
    c.run();
    c.rows({0x1e, 0x0e, 0x06, 0x02, 0});
}

TEST(rdp_fill_triangle_right_major_uses_all_subpixel_extents) {
    TriangleFill c;
    c.triangle(0, 16, 16, 0x40000, 0x10000, 0, 0, 0x10000, 0, false);
    c.run();
    c.rows({0x1e, 0x1c, 0x18, 0x10, 0});
}

TEST(rdp_fill_triangle_all_opcodes_ignore_color_texture_and_depth_attributes) {
    for (unsigned opcode = 8; opcode < 16; ++opcode) {
        TriangleFill c;
        c.triangle(0, 16, 16, 0x10000, 0x40000, 0, 0, -0x10000, 0, true, opcode);
        c.run();
        c.rows({0x1e, 0x0e, 0x06, 0x02, 0});
    }
}

TEST(rdp_fill_triangle_switches_minor_edge_at_fractional_middle_y) {
    TriangleFill c;
    c.triangle(0, 6, 12, 0x10000, 0x20000, 0x50000, 0, 0, -0x10000);
    c.run();
    c.rows({0x06, 0x3e, 0x1e, 0, 0});
}

TEST(rdp_fill_triangle_fractional_top_uses_whole_row_interpolation_origin) {
    TriangleFill c;
    c.triangle(3, 12, 9, 0x10000, 0x50000, 0, 0x10000, 0);
    c.run();
    c.rows({0x3e, 0x3c, 0x38, 0, 0});
}

TEST(rdp_fill_triangle_fractional_scissor_and_field_selection) {
    for (unsigned field : {0U, 2U, 3U}) {
        TriangleFill c;
        c.scissor(9, 5, 17, 13, field);
        c.triangle(0, 16, 16, 0, 0x60000, 0, 0, 0);
        c.run();
        c.rows({0, field == 2 ? 0U : 0x1cU, field == 3 ? 0U : 0x1cU, field == 2 ? 0U : 0x1cU, 0});
    }
}

TEST(rdp_fill_triangle_clips_negative_coordinates_and_excludes_bottom) {
    TriangleFill c;
    c.triangle(-4, 8, 8, -0x10000, 0x20000, 0, 0, 0);
    c.run();
    c.rows({7, 7, 0, 0, 0});
}

TEST(rdp_fill_triangle_middle_before_start_uses_lower_edge) {
    TriangleFill c;
    c.triangle(4, 0, 12, 0x10000, 0x20000, 0x60000, 0, 0);
    c.run();
    c.rows({0, 0x7e, 0x7e, 0, 0});
}

TEST(rdp_fill_triangle_masks_reserved_x_bits) {
    TriangleFill c;
    c.triangle(0, 8, 8, 0x10010000, 0x10040000, 0, 0, 0);
    c.run();
    c.rows({0x1e, 0x1e, 0, 0, 0});
}

TEST(rdp_fill_triangle_wraps_edge_accumulators_at_28_bits) {
    TriangleFill c;
    c.triangle(0, 8, 8, 0x07ff0000, 0x20000, 0, 0x10000, 0);
    c.run();
    c.rows({0, 7, 0, 0, 0});
}

TEST(rdp_fill_triangle_quantizes_slopes_before_stepping) {
    for (s32 slope : {7, 8}) {
        TriangleFill c;
        c.triangle(0, 4, 4, 0, 0xfffe, 0, 0, slope);
        c.run();
        c.rows({slope == 7 ? 1U : 3U, 0, 0, 0, 0});
    }
}

TEST(rdp_fill_triangle_edge_order_uses_quarter_pixel_precision) {
    for (s32 left : {0x3ffe, 0x4000}) {
        TriangleFill c;
        c.triangle(0, 4, 4, left, 0, 0, 0, 0);
        c.run();
        c.rows({left == 0x3ffe ? 1U : 0U, 0, 0, 0, 0});
    }
}

TEST(rdp_fill_triangle_framebuffer_width_is_stride_not_clip) {
    TriangleFill c(3, 0, 2);
    c.triangle(0, 4, 4, 0x10000, 0x40000, 0, 0, 0);
    c.run();
    c.rows({0x1e, 0, 0, 0, 0});
}

TEST(rdp_fill_triangle_hidden_pairs_follow_fill_bytes) {
    for (unsigned size = 1; size <= 3; ++size) {
        TriangleFill c(size);
        for (unsigned address = 0x8000; address < 0x8020; address += 2)
            c.system->bus.memory.set_hidden_pair(address, 2);
        c.triangle(0, 4, 4, 0x20000, 0x20000, 0, 0, 0);
        c.run();
        const unsigned bytes = 1U << (size - 1U);
        const unsigned target = 0x8000U + 2U * bytes;
        for (unsigned address = 0x8000; address < 0x8020; address += 2) {
            u8 expected = 2;
            if (size > 1 && address >= target && address < target + bytes)
                expected = 3;
            CHECK_EQ(c.system->bus.memory.hidden_pair(address), expected);
        }
    }
}

TEST(rdp_fill_triangle_reversed_or_clipped_spans_do_not_write) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        TriangleFill c;
        if (mode == 0)
            c.triangle(0, 8, 8, 0x40000, 0x10000, 0, 0, 0);
        else if (mode == 1)
            c.triangle(8, 8, 4);
        else {
            c.scissor(0, 0, mode == 2 ? 0U : 32U, mode == 3 ? 0U : 20U);
            c.triangle();
        }
        c.run();
        c.rows({0, 0, 0, 0, 0});
    }
}

TEST(rdp_fill_triangle_packs_all_destination_sizes_independent_of_format) {
    for (unsigned size = 1; size <= 3; ++size)
        for (unsigned format = 0; format < 8; ++format) {
            TriangleFill c(size, format, 5);
            c.triangle(0, 8, 8, 0x10000, 0x30000, 0, 0, 0);
            c.run();
            const unsigned bytes = 1U << (size - 1U);
            for (unsigned pixel = 0; pixel < 15; ++pixel) {
                const bool written = pixel < 10 && pixel % 5 >= 1 && pixel % 5 <= 3;
                const unsigned address = 0x8000U + pixel * bytes;
                const unsigned shift = (4U - bytes - (address & (4U - bytes))) * 8U;
                const u64 mask = (1ULL << (bytes * 8U)) - 1U;
                CHECK_EQ(c.system->bus.memory.read(address, bytes),
                         written ? (TriangleFill::color >> shift) & mask : 0ULL);
            }
        }
}

TEST(rdp_fill_triangle_waits_for_last_attribute_word) {
    TriangleFill c;
    c.triangle(0, 4, 4, 0x10000, 0x20000, 0, 0, 0, 0, true, 15);
    c.system->bus.rdp.write_register(0, 0x1000);
    c.system->bus.rdp.write_register(4, c.end - 8U);
    CHECK_EQ(c.system->bus.memory.read(0x8004, 4), 0ULL);
    c.system->bus.rdp.write_register(4, c.end);
    CHECK_EQ(c.system->bus.memory.read(0x8004, 4), static_cast<u64>(TriangleFill::color));
}
