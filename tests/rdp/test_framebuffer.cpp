#include "copy_commands.hpp"
#include "framebuffer_commands.hpp"

using namespace cupid;
using namespace test::rdp;

TEST(rdp_color_rectangle_stores_every_framebuffer_size_and_format) {
    for (bool two_cycles : {false, true})
        for (unsigned size = 0; size < 4; ++size)
            for (unsigned format = 0; format < 8; ++format) {
                FramebufferCommands commands(size, format);
                commands.modes(static_cast<u64>(two_cycles) << 52U);
                commands.rectangle(4, 0, 16, 8);
                commands.run();
                for (unsigned y = 0; y < 3; ++y)
                    for (unsigned x = 0; x < 5; ++x) {
                        const u32 clear = size < 2 ? 0xa5U : size == 2 ? 0xa5a5U : 0xa5a5a5a5U;
                        CHECK_EQ(commands.pixel(x, y),
                                 y < 2 && x >= 1 && x < 4 ? commands.solid(x, y) : clear);
                    }
            }
}

TEST(rdp_color_triangles_store_every_framebuffer_size_format_and_opcode) {
    for (bool two_cycles : {false, true})
        for (unsigned size = 0; size < 4; ++size)
            for (unsigned format = 0; format < 8; ++format)
                for (unsigned opcode = 8; opcode < 16; ++opcode) {
                    FramebufferCommands commands(size, format);
                    commands.modes(static_cast<u64>(two_cycles) << 52U);
                    commands.triangle(opcode, {.major = 0x10000});
                    commands.run();
                    CHECK_EQ(commands.pixel(1), commands.solid(1));
                    CHECK_EQ(commands.pixel(3, 1), commands.solid(3, 1));
                    CHECK_EQ(commands.pixel(), size < 2 ? 0xa5U : size == 2 ? 0xa5a5U : 0xa5a5a5a5U);
                }
}

TEST(rdp_four_bit_color_draws_clear_bytes_and_preserve_hidden_pairs) {
    for (unsigned x = 0; x < 4; ++x) {
        FramebufferCommands commands(0);
        commands.rectangle(x * 4U, 0, (x + 1U) * 4U, 4);
        commands.run();
        for (unsigned index = 0; index < 5; ++index) {
            CHECK_EQ(commands.pixel(index), index == x ? 0U : 0xa5U);
            CHECK_EQ(commands.system->bus.memory.hidden_pair(commands.address(index)), 2U);
        }
    }
}

TEST(rdp_eight_bit_color_writes_select_channels_by_absolute_address_parity) {
    for (unsigned origin = 0; origin < 2; ++origin) {
        FramebufferCommands commands(1, 0, 3, 0x8000U + origin);
        commands.rectangle(0, 0, 12, 8);
        commands.run();
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 3; ++x)
                CHECK_EQ(commands.pixel(x, y), commands.solid(x, y));
        CHECK_EQ(commands.pixel(0, 2), 0xa5U);
    }
}

TEST(rdp_eight_bit_color_writes_update_hidden_bits_only_on_odd_bytes) {
    for (unsigned x : {0U, 1U})
        for (unsigned green : {0x42U, 0x43U}) {
            FramebufferCommands commands(1);
            commands.append(0x3a, 0x810025ffU | (static_cast<u64>(green) << 16U));
            commands.rectangle(x * 4U, 0, (x + 1U) * 4U, 4);
            commands.run();
            CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), x == 0U ? 2U : (green & 1U) * 3U);
        }
}

TEST(rdp_intensity16_packs_red_and_coverage_without_rgba_hidden_coverage) {
    FramebufferCommands commands(2, 3);
    commands.modes(8U);
    commands.rectangle(0, 0, 4, 2);
    commands.run();
    CHECK_EQ(commands.pixel(), 0x8160U);
    CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), 0U);
}

TEST(rdp_framebuffer_memory_input_decodes_intensity_and_format_aliases) {
    for (unsigned size = 0; size < 4; ++size)
        for (unsigned format = 0; format < 8; ++format) {
            FramebufferCommands commands(size, format);
            commands.system->bus.memory.write(commands.address(0), commands.bytes(), 0x12345640);
            commands.modes((1ULL << 30U) | (1ULL << 6U) | (3ULL << 8U));
            commands.rectangle();
            commands.run();
            const u32 expected = size == 0 ? 0U : size == 1 ? 0x40U : size == 2 ? 0x5640U : 0x12345640U;
            CHECK_EQ(commands.pixel(), expected);
        }
}

TEST(rdp_framebuffer_blending_reads_intensity_into_all_color_channels) {
    for (bool image_read : {false, true})
        for (unsigned size : {1U, 2U, 3U}) {
            FramebufferCommands commands(size, 3);
            const u32 stored = size == 1U ? 0x80U : size == 2U ? 0x8080U : 0x808080e0U;
            for (unsigned x = 0; x < 2; ++x)
                commands.system->bus.memory.write(commands.address(x), commands.bytes(), stored);
            commands.append(0x3a, 0x20406080);
            commands.modes((1ULL << 14U) | (1ULL << 22U) | (static_cast<u64>(image_read) << 6U));
            commands.rectangle(0, 0, 8, 4);
            commands.run();
            CHECK_EQ(commands.pixel(), size == 1U ? 0x50U : size == 2U ? 0x50e0U : 0x506070e0U);
            CHECK_EQ(commands.pixel(1), size == 1U ? 0x60U : size == 2U ? 0x50e0U : 0x506070e0U);
        }
}

TEST(rdp_intensity16_coverage_reads_ignore_low_alpha_bits_and_hidden_pairs) {
    for (bool image_read : {false, true})
        for (unsigned coverage = 0; coverage < 8; ++coverage)
            for (unsigned hidden = 0; hidden < 4; ++hidden) {
                FramebufferCommands commands(2, 7);
                commands.system->bus.memory.write(0x8000, 2, 0x801fU | (coverage << 5U));
                commands.system->bus.memory.set_hidden_pair(0x8000, static_cast<u8>(hidden));
                commands.modes((3ULL << 8U) | (static_cast<u64>(image_read) << 6U));
                commands.rectangle();
                commands.run();
                CHECK_EQ(commands.pixel(), 0x8100U | ((image_read ? coverage : 7U) << 5U));
                CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), 0U);
            }
}

TEST(rdp_eight_bit_framebuffer_supplies_full_memory_coverage_for_blending) {
    for (unsigned hidden = 0; hidden < 4; ++hidden) {
        FramebufferCommands commands(1);
        commands.system->bus.memory.write(0x8000, 2, 0x8080);
        commands.system->bus.memory.set_hidden_pair(0x8000, static_cast<u8>(hidden));
        commands.append(0x3a, 0x20406080);
        commands.append(0x2e, 0xffff);
        commands.modes((1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U) | 4U | 64U);
        commands.rectangle(0, 0, 8, 4);
        commands.run();
        CHECK_EQ(commands.pixel(), 0x90U);
        CHECK_EQ(commands.pixel(1), 0xa0U);
    }
}

TEST(rdp_intensity16_memory_alpha_controls_blender_factor) {
    FramebufferCommands commands(2, 1);
    commands.system->bus.memory.write(0x8000, 2, 0x804f);
    commands.append(0x3a, 0x20406080);
    commands.append(0x2e, 0xffff);
    commands.modes((1ULL << 14U) | (1ULL << 22U) | (1ULL << 18U) | 4U | 64U);
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x40e0U);
}

TEST(rdp_framebuffer_origins_align_to_element_size_before_row_stride) {
    for (unsigned size = 0; size < 4; ++size)
        for (unsigned low = 0; low < 4; ++low) {
            FramebufferCommands commands(size, 3, 3, 0x8000U + low);
            commands.rectangle(0, 4, 12, 8);
            commands.run();
            for (unsigned x = 0; x < 3; ++x) {
                CHECK_EQ(commands.pixel(x, 1), commands.solid(x, 1));
                CHECK_EQ(commands.pixel(x), size < 2 ? 0xa5U : size == 2 ? 0xa5a5U : 0xa5a5a5a5U);
            }
        }
}

TEST(rdp_framebuffer_color_and_depth_wrap_at_installed_memory_size) {
    for (u32 memory_size : {0x400000U, 0x800000U})
        for (unsigned size = 0; size < 4; ++size) {
            FramebufferCommands commands(size, 3, 3);
            commands.system->bus.rdram.resize(memory_size);
            test::initialize_memory(*commands.system);
            commands.end = 0x1000;
            commands.append(0x3c, combine_word({}, {}));
            commands.append(0x3a, 0x814325ff);
            const unsigned bytes = commands.bytes();
            const u32 base = memory_size - bytes;
            commands.append(0x3f, (3ULL << 53U) | (static_cast<u64>(size) << 51U) | (2ULL << 32U) | base);
            commands.append(0x3e, memory_size + 0x9001U);
            commands.append(0x2e, 0x40000020);
            commands.modes(4U | 32U);
            commands.rectangle(0, 0, 12, 8);
            commands.run();
            for (unsigned index = 0; index < 6; ++index) {
                const u32 address = (base + index * bytes) & (memory_size - 1U);
                const u32 color = size == 0   ? 0U
                                  : size == 1 ? ((address & 1U) != 0 ? 0x43U : 0x81U)
                                  : size == 2 ? 0x81e0U
                                              : 0x814325e0U;
                CHECK_EQ(commands.system->bus.memory.read(address, bytes), color);
                CHECK_EQ(commands.system->bus.memory.read(0x9000U + index * 2U, 2), 0x2001U);
                CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9000U + index * 2U), 1U);
            }
        }
}

TEST(rdp_framebuffer_depth_stride_remains_two_bytes_for_narrow_color) {
    for (unsigned size = 0; size < 4; ++size) {
        FramebufferCommands commands(size, 3, 3);
        commands.append(0x2e, 0x40000020);
        commands.modes(4U | 32U);
        commands.rectangle(4, 4, 8, 8);
        commands.run();
        CHECK_EQ(commands.pixel(1, 1), commands.solid(1, 1));
        CHECK_EQ(commands.system->bus.memory.read(0x9008, 2), 0x2001U);
        CHECK_EQ(commands.system->bus.memory.hidden_pair(0x9008), 1U);
        CHECK_EQ(commands.system->bus.memory.read(0x9006, 2), 0U);
        CHECK_EQ(commands.system->bus.memory.read(0x900a, 2), 0U);
    }
}

TEST(rdp_framebuffer_fields_clip_writes_for_every_size) {
    for (unsigned size = 0; size < 4; ++size) {
        FramebufferCommands commands(size, 3);
        commands.append(0x2d, (3ULL << 24U) | (20ULL << 12U) | 16U);
        commands.rectangle(0, 0, 20, 16);
        commands.run();
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 5; ++x)
                CHECK_EQ(commands.pixel(x, y), (y & 1U) != 0 ? commands.solid(x, y)
                                               : size < 2    ? 0xa5U
                                               : size == 2   ? 0xa5a5U
                                                             : 0xa5a5a5a5U);
    }
}

TEST(rdp_framebuffer_alpha_rejection_preserves_all_sizes_and_hidden_bits) {
    for (unsigned size = 0; size < 4; ++size) {
        FramebufferCommands commands(size, 3);
        commands.append(0x3a, 0x81432500);
        commands.append(0x39, 255);
        commands.modes(1U);
        commands.rectangle();
        commands.run();
        CHECK_EQ(commands.pixel(), size < 2 ? 0xa5U : size == 2 ? 0xa5a5U : 0xa5a5a5a5U);
        CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), 2U);
    }
}

TEST(rdp_fill_framebuffer_writes_wrap_without_changing_word_lane_selection) {
    for (unsigned size = 1; size < 4; ++size) {
        FramebufferCommands commands(size, 3, 3);
        const unsigned bytes = commands.bytes();
        const u32 base = 0x800000U - bytes;
        commands.append(0x3f, (3ULL << 53U) | (static_cast<u64>(size) << 51U) | (2ULL << 32U) | base);
        commands.append(0x2f, 3ULL << 52U);
        commands.append(0x37, 0x12345679);
        commands.rectangle(0, 0, 8, 0);
        commands.run();
        for (unsigned x = 0; x < 3; ++x) {
            const u32 address = (base + x * bytes) & 0x7fffffU;
            const unsigned shift = (4U - bytes - (address & (4U - bytes))) * 8U;
            const u32 expected =
                bytes == 4U ? 0x12345679U : (0x12345679U >> shift) & ((1U << (bytes * 8U)) - 1U);
            CHECK_EQ(commands.system->bus.memory.read(address, bytes), expected);
        }
    }
}

TEST(rdp_copy_framebuffer_writes_wrap_and_preserve_hidden_bits) {
    for (unsigned size : {1U, 2U}) {
        CopyCommands commands(size);
        const unsigned bytes = size == 1U ? 1U : 2U;
        const u32 base = 0x800000U - bytes;
        commands.append(0x3f, (static_cast<u64>(size) << 51U) | (15ULL << 32U) | base);
        commands.rectangle(0, 0, 4, 0);
        commands.run();
        CHECK_EQ(commands.system->bus.memory.read(base, bytes), size == 1U ? 0x10U : 0x1001U);
        CHECK_EQ(commands.system->bus.memory.read(0, bytes), size == 1U ? 1U : 0x1003U);
        CHECK_EQ(commands.system->bus.memory.hidden_pair(0), size == 1U ? 0U : 3U);
    }
}

TEST(rdp_framebuffer_depth_updates_follow_color_when_halfwords_overlap) {
    for (unsigned format = 0; format < 8; ++format) {
        FramebufferCommands commands(2, format);
        commands.append(0x3e, 0x8000);
        commands.append(0x2e, 0x40000080);
        commands.modes(4U | 32U);
        commands.rectangle();
        commands.run();
        CHECK_EQ(commands.pixel(), 0x2001U);
        CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), 3U);
    }
}

TEST(rdp_framebuffer_narrow_color_depth_rejection_preserves_neighboring_bytes) {
    for (unsigned size : {0U, 1U, 2U}) {
        FramebufferCommands commands(size, 3);
        commands.system->bus.memory.write(0x9000, 2, 0);
        commands.append(0x2e, 0x70000001);
        commands.modes(4U | 16U | 32U);
        commands.rectangle();
        commands.run();
        CHECK_EQ(commands.pixel(), size < 2 ? 0xa5U : 0xa5a5U);
        CHECK_EQ(commands.pixel(1), size < 2 ? 0xa5U : 0xa5a5U);
        CHECK_EQ(commands.system->bus.memory.hidden_pair(0x8000), 2U);
        CHECK_EQ(commands.system->bus.memory.read(0x9000, 2), 0U);
    }
}
