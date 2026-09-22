#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <memory>

namespace {

using namespace cupid;

constexpr u32 framebuffer = 0x8000;
constexpr u32 fill = 0x12355678;

struct FillCommands {
    std::unique_ptr<System> system = std::make_unique<System>();
    u32 end = 0x1000;

    FillCommands(unsigned size = 3, unsigned format = 0, unsigned width = 8, u32 address = framebuffer) {
        test::initialize_memory(*system);
        append(0x3f, (static_cast<u64>(format) << 53U) | (static_cast<u64>(size) << 51U) |
                         (static_cast<u64>(width - 1U) << 32U) | address);
        append(0x2f, 3ULL << 52U);
        append(0x37, fill);
    }

    void append(unsigned opcode, u64 payload) {
        system->bus.write(end, 8, (static_cast<u64>(opcode) << 56U) | payload);
        end += 8;
    }

    void scissor(unsigned left, unsigned top, unsigned right, unsigned bottom, unsigned field = 0) {
        append(0x2d, (static_cast<u64>(left) << 44U) | (static_cast<u64>(top) << 32U) |
                         (static_cast<u64>(field) << 24U) | (static_cast<u64>(right) << 12U) | bottom);
    }

    void rectangle(unsigned left, unsigned top, unsigned right, unsigned bottom) {
        append(0x36, (static_cast<u64>(right) << 44U) | (static_cast<u64>(bottom) << 32U) |
                         (static_cast<u64>(left) << 12U) | top);
    }

    void run() {
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
        CHECK_EQ(system->bus.rdp.current(), end);
    }

    void check_box(unsigned left, unsigned top, unsigned right, unsigned bottom, unsigned field = 0) {
        for (unsigned y = 0; y < 5; ++y) {
            for (unsigned x = 0; x < 8; ++x) {
                const bool selected = field < 2 || (y & 1U) == (field & 1U);
                const bool written = x >= left && x <= right && y >= top && y <= bottom && selected;
                CHECK_EQ(system->bus.memory.read(framebuffer + (y * 8U + x) * 4U, 4),
                         written ? static_cast<u64>(fill) : 0ULL);
            }
        }
    }
};

} // namespace

TEST(rdp_fill_fractional_scissor_keeps_intersecting_pixels) {
    FillCommands commands;
    commands.scissor(5, 5, 13, 13);
    commands.rectangle(0, 0, 24, 16);
    commands.run();
    commands.check_box(1, 1, 3, 3);
}

TEST(rdp_fill_right_scissor_edge_is_inclusive_after_span_clipping) {
    FillCommands commands;
    commands.scissor(4, 4, 12, 12);
    commands.rectangle(0, 0, 24, 16);
    commands.run();
    commands.check_box(1, 1, 3, 2);
}

TEST(rdp_fill_zero_scissor_does_not_disable_clipping) {
    for (unsigned axis = 0; axis < 2; ++axis) {
        FillCommands commands;
        commands.scissor(0, 0, axis == 0 ? 0U : 32U, axis == 1 ? 0U : 20U);
        commands.rectangle(0, 0, 24, 16);
        commands.run();
        commands.check_box(8, 5, 8, 5);
    }
}

TEST(rdp_fill_rejects_reversed_subpixel_edges) {
    FillCommands commands;
    commands.rectangle(7, 0, 4, 8);
    commands.run();
    commands.check_box(8, 5, 8, 5);
}

TEST(rdp_fill_bottom_extension_still_requires_a_valid_subpixel_row) {
    for (unsigned top = 0; top < 4; ++top) {
        FillCommands commands;
        commands.rectangle(4, top, 8, 0);
        commands.run();
        if (top < 3)
            commands.check_box(1, 0, 2, 0);
        else
            commands.check_box(8, 5, 8, 5);
    }
}

TEST(rdp_fill_scissor_filters_both_fields_and_can_disable_filtering) {
    for (unsigned field = 0; field < 4; ++field) {
        FillCommands commands;
        commands.scissor(0, 0, 32, 20, 3);
        commands.scissor(0, 0, 32, 20, field);
        commands.rectangle(4, 0, 8, 16);
        commands.run();
        commands.check_box(1, 0, 2, 4, field);
    }
}

TEST(rdp_fill_8bit_selects_bytes_by_address_across_odd_width_rows) {
    for (unsigned format = 0; format < 8; ++format) {
        FillCommands commands(1, format, 3);
        commands.rectangle(0, 0, 8, 8);
        commands.run();
        for (unsigned index = 0; index < 12; ++index) {
            const u64 expected = index < 9 ? (fill >> ((3U - (index & 3U)) * 8U)) & 0xffU : 0U;
            CHECK_EQ(commands.system->bus.memory.read(framebuffer + index, 1), expected);
        }
    }
}

TEST(rdp_fill_8bit_even_writes_preserve_hidden_bits_and_odd_writes_replace_them) {
    for (unsigned x = 0; x < 4; ++x) {
        FillCommands commands(1);
        auto& memory = commands.system->bus.memory;
        for (unsigned index = 0; index < 4; index += 2)
            memory.set_hidden_pair(framebuffer + index, 2);
        commands.rectangle(x * 4U, 0, x * 4U, 0);
        commands.run();
        for (unsigned index = 0; index < 4; index += 2) {
            const u8 expected = x == index + 1 ? (x == 1 ? 3 : 0) : 2;
            CHECK_EQ(memory.hidden_pair(framebuffer + index), expected);
        }
    }
}

TEST(rdp_fill_16bit_and_32bit_ignore_format_and_write_hidden_bits) {
    for (unsigned size = 2; size <= 3; ++size) {
        for (unsigned format = 0; format < 8; ++format) {
            FillCommands commands(size, format, 3);
            auto& memory = commands.system->bus.memory;
            commands.rectangle(0, 0, 8, 4);
            commands.run();
            const unsigned bytes = 6U * (1U << (size - 1U));
            for (unsigned index = 0; index < bytes; index += 2) {
                const u16 expected = static_cast<u16>(fill >> ((index & 2U) == 0 ? 16U : 0U));
                CHECK_EQ(memory.read(framebuffer + index, 2), expected);
                CHECK_EQ(memory.hidden_pair(framebuffer + index), (expected & 1U) * 3U);
            }
            CHECK_EQ(memory.read(framebuffer + bytes, 4), 0ULL);
        }
    }
}

TEST(rdp_fill_color_image_masks_reserved_width_and_address_bits) {
    FillCommands commands(3, 0, 0xc08, framebuffer | 0x03000000U);
    commands.rectangle(4, 4, 8, 8);
    commands.run();
    commands.check_box(1, 1, 2, 2);
}

TEST(rdp_fill_rejects_spans_outside_scissor_but_keeps_left_edge_contact) {
    for (unsigned edge = 0; edge < 3; ++edge) {
        FillCommands commands;
        commands.scissor(8, 0, 16, 20);
        if (edge == 0)
            commands.rectangle(0, 0, 7, 8);
        else if (edge == 1)
            commands.rectangle(16, 0, 20, 8);
        else
            commands.rectangle(0, 0, 8, 8);
        commands.run();
        if (edge == 2)
            commands.check_box(2, 0, 2, 2);
        else
            commands.check_box(8, 5, 8, 5);
    }
}

TEST(rdp_fill_8bit_color_selection_includes_framebuffer_base_offset) {
    for (unsigned offset = 0; offset < 4; ++offset) {
        FillCommands commands(1, 0, 8, framebuffer + offset);
        commands.rectangle(0, 0, 12, 0);
        commands.run();
        for (unsigned index = 0; index < 8; ++index) {
            const bool written = index >= offset && index < offset + 4;
            const u64 expected = written ? (fill >> ((3U - (index & 3U)) * 8U)) & 0xffU : 0U;
            CHECK_EQ(commands.system->bus.memory.read(framebuffer + index, 1), expected);
        }
    }
}

TEST(rdp_fill_reset_clears_scissor_field_selection) {
    FillCommands commands;
    commands.scissor(0, 0, 32, 20, 3);
    commands.run();
    commands.system->bus.rdp.reset();
    commands.append(0x3f, (3ULL << 51U) | (7ULL << 32U) | framebuffer);
    commands.append(0x2f, 3ULL << 52U);
    commands.append(0x37, fill);
    commands.rectangle(4, 0, 8, 16);
    commands.system->bus.rdp.write_register(0, 0x1020);
    commands.system->bus.rdp.write_register(4, commands.end);
    commands.check_box(1, 0, 2, 4);
}

TEST(rdp_unshaded_triangle_obeys_scissor_field_selection) {
    for (unsigned field = 0; field < 4; ++field) {
        FillCommands commands;
        commands.scissor(0, 0, 32, 20, field);
        commands.append(0x2f, 2ULL << 8U);
        commands.append(0x08, (1ULL << 55U) | (20ULL << 32U) | (20ULL << 16U));
        commands.append(0, 2ULL << 48U);
        commands.append(0, 0);
        commands.append(0, 2ULL << 48U);
        commands.run();
        for (unsigned y = 0; y < 5; ++y) {
            for (unsigned x = 0; x < 8; ++x) {
                const bool selected = field < 2 || (y & 1U) == (field & 1U);
                CHECK_EQ(commands.system->bus.memory.read(framebuffer + (y * 8U + x) * 4U, 4),
                         x < 2 && selected ? 0x000000e0ULL : 0ULL);
            }
        }
    }
}
