#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {

using namespace cupid;

constexpr u32 image_address = 0x10000;

u8 source_byte(unsigned offset) {
    return static_cast<u8>((offset * 37U + (offset >> 8U) * 11U + 1U) & 0xffU);
}

struct TextureCommands {
    std::unique_ptr<System> system = std::make_unique<System>();
    std::array<u8, 4096> expected{};
    u32 end = 0x1000;

    TextureCommands() {
        test::initialize_memory(*system);
        for (unsigned index = 0; index < 0x10000; ++index)
            system->bus.memory.write(image_address + index, 1, source_byte(index));
    }

    void append(unsigned opcode, u64 payload = 0) {
        system->bus.write(end, 8, (static_cast<u64>(opcode) << 56U) | payload);
        end += 8;
    }

    void image(unsigned size, unsigned width = 16, unsigned offset = 0) {
        append(0x3d, (static_cast<u64>(size) << 51U) | (static_cast<u64>(width - 1U) << 32U) |
                         (image_address + offset));
    }

    void tile(unsigned size, unsigned stride, unsigned offset, unsigned index = 0, unsigned format = 0,
              u32 sampling = 0) {
        append(0x35, (static_cast<u64>(format) << 53U) | (static_cast<u64>(size) << 51U) |
                         (static_cast<u64>(stride / 8U) << 41U) | (static_cast<u64>(offset / 8U) << 32U) |
                         (static_cast<u64>(index) << 24U) | sampling);
    }

    void bounds(unsigned opcode, unsigned sl, unsigned tl, unsigned sh, unsigned th, unsigned index = 0) {
        append(opcode, (static_cast<u64>(sl) << 44U) | (static_cast<u64>(tl) << 32U) |
                           (static_cast<u64>(index) << 24U) | (static_cast<u64>(sh) << 12U) | th);
    }

    void run() {
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
    }

    void word(unsigned destination, unsigned source, unsigned swap = 0) {
        for (unsigned byte = 0; byte < 8; ++byte)
            expected[(destination + (byte ^ swap)) & 0xfffU] = source_byte(source + byte);
    }

    void split_word(unsigned destination, unsigned source, bool odd = false, bool yuv = false) {
        for (unsigned pixel = 0; pixel < 2; ++pixel) {
            for (unsigned bank = 0; bank < 2; ++bank) {
                for (unsigned byte = 0; byte < 2; ++byte) {
                    const unsigned address = ((destination + pixel * 2U + byte) ^ (odd ? 4U : 0U)) & 0x7ffU;
                    const unsigned input = yuv ? byte * 2U + bank : bank * 2U + byte;
                    expected[address + bank * 0x800U] = source_byte(source + pixel * 4U + input);
                }
            }
        }
    }

    void palette_word(unsigned destination, unsigned source) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            const unsigned input = source + ((source & 1U) != 0 ? lane * 2U : 0U);
            expected[(destination + lane * 2U) & 0xfffU] = source_byte(input);
            expected[(destination + lane * 2U + 1U) & 0xfffU] = source_byte(input + 1U);
        }
    }

    void check() const {
        const auto& actual = system->bus.rdp.texture_memory();
        for (unsigned byte = 0; byte < actual.size(); ++byte)
            CHECK_EQ(actual[byte], expected[byte]);
    }
};

} // namespace

TEST(rdp_texture_tile_descriptors_decode_all_fields_and_preserve_bounds) {
    TextureCommands commands;
    for (unsigned index = 0; index < 8; ++index) {
        commands.bounds(0x32, 0x123, 0x456, 0xabc, 0xdef, index);
        commands.tile(index & 3U, 4088, index * 8U, index, index, 0x00fdbbadU);
    }
    commands.run();
    for (unsigned index = 0; index < 8; ++index) {
        const auto& tile = commands.system->bus.rdp.tile(index);
        CHECK_EQ(tile.tmem_address, index * 8U);
        CHECK_EQ(tile.line_stride, 4088U);
        CHECK_EQ(tile.format, index);
        CHECK_EQ(tile.size, index & 3U);
        CHECK_EQ(tile.palette, 15U);
        CHECK_EQ(tile.s_shift, 13U);
        CHECK_EQ(tile.s_mask, 10U);
        CHECK_EQ(tile.t_shift, 14U);
        CHECK_EQ(tile.t_mask, 6U);
        CHECK(tile.s_mirror);
        CHECK(tile.s_clamp);
        CHECK(tile.t_mirror);
        CHECK(tile.t_clamp);
        CHECK_EQ(tile.s_low, 0x123U);
        CHECK_EQ(tile.t_low, 0x456U);
        CHECK_EQ(tile.s_high, 0xabcU);
        CHECK_EQ(tile.t_high, 0xdefU);
    }
}

TEST(rdp_texture_tile_load_rounds_to_words_and_swaps_odd_rows) {
    for (unsigned size = 1; size <= 2; ++size) {
        TextureCommands commands;
        commands.image(size, 13, 1);
        commands.tile(size, 24, 40, 5);
        commands.bounds(0x34, 5, 7, size == 1 ? 37U : 21U, 12, 5);
        commands.run();
        const unsigned bytes = 1U << (size - 1U);
        for (unsigned row = 0; row < 3; ++row) {
            const unsigned source = 1U + ((row + 1U) * 13U + 1U) * bytes;
            commands.word(40U + row * 24U, source, (row & 1U) * 4U);
            commands.word(48U + row * 24U, source + 8U, (row & 1U) * 4U);
        }
        commands.check();
        const auto& tile = commands.system->bus.rdp.tile(5);
        CHECK_EQ(tile.s_low, 5U);
        CHECK_EQ(tile.t_low, 7U);
        CHECK_EQ(tile.t_high, 12U);
    }
}

TEST(rdp_texture_rgba32_load_splits_banks_and_swaps_odd_rows) {
    TextureCommands commands;
    commands.image(3, 7, 3);
    commands.tile(3, 16, 0x7f8);
    commands.bounds(0x34, 4, 4, 12, 8);
    commands.run();
    commands.split_word(0x7f8, 35);
    commands.split_word(0x7fc, 43);
    commands.split_word(0x808, 63, true);
    commands.split_word(0x80c, 71, true);
    commands.check();
}

TEST(rdp_texture_yuv_load_separates_chroma_and_luma) {
    TextureCommands commands;
    commands.image(2, 8);
    commands.tile(2, 8, 0, 0, 1);
    commands.bounds(0x34, 0, 0, 12, 4);
    commands.run();
    commands.split_word(0, 0, false, true);
    commands.split_word(8, 16, true, true);
    commands.check();
}

TEST(rdp_texture_zero_stride_and_overlapping_rows_keep_last_writes) {
    for (unsigned stride : {0U, 8U}) {
        TextureCommands commands;
        commands.image(1, 24);
        commands.tile(1, stride, 0xff8);
        commands.bounds(0x34, 0, 0, 60, 8);
        commands.run();
        for (unsigned row = 0; row < 3; ++row) {
            commands.word(0xff8U + row * stride, row * 24U, (row & 1U) * 4U);
            commands.word(0x1000U + row * stride, row * 24U + 8U, (row & 1U) * 4U);
        }
        commands.check();
    }
}

TEST(rdp_texture_block_uses_integer_coordinates_and_dxt_with_stride) {
    for (unsigned stride : {0U, 8U}) {
        TextureCommands commands;
        commands.image(2, 16);
        commands.tile(2, stride, 0xff0);
        commands.bounds(0x33, 2, 1, 17, 0x600);
        commands.run();
        commands.word(0xff0, 36);
        commands.word(0xff8, 44);
        commands.word(0x1000 + stride, 52, 4);
        commands.word(0x1008 + stride * 2U, 60);
        commands.check();
    }
}

TEST(rdp_texture_block_rgba32_dxt_can_overwrite_or_skip_word_pairs) {
    TextureCommands commands;
    commands.image(3);
    commands.tile(3, 0, 0);
    commands.bounds(0x33, 0, 0, 7, 0x800);
    commands.run();
    commands.split_word(0, 8);
    commands.split_word(8, 24);
    commands.check();
}

TEST(rdp_texture_tlut_replicates_palette_entries_and_wraps) {
    TextureCommands commands;
    commands.image(2, 8);
    commands.tile(0, 0, 0xff8, 7);
    commands.bounds(0x30, 4, 0, 12, 0, 7);
    commands.run();
    commands.palette_word(0xff8, 2);
    commands.palette_word(0, 4);
    commands.palette_word(8, 6);
    commands.check();
}

TEST(rdp_texture_tlut_odd_source_address_selects_separate_bank_halfwords) {
    TextureCommands commands;
    commands.image(2, 8, 1);
    commands.tile(0, 0, 0x800);
    commands.bounds(0x30, 0, 0, 4, 0);
    commands.run();
    commands.palette_word(0x800, 1);
    commands.palette_word(0x808, 3);
    commands.check();
}

TEST(rdp_texture_image_reserved_bits_do_not_change_source_width_or_address) {
    TextureCommands commands;
    commands.image(1, 0xc10, 0x03000000U);
    commands.tile(1, 8, 0);
    commands.bounds(0x34, 0, 4, 0, 4);
    commands.run();
    commands.word(0, 16);
    commands.check();
}

TEST(rdp_texture_mismatched_sizes_create_mirrors_and_unwritten_gaps) {
    struct Case {
        unsigned image_size;
        unsigned tile_size;
        unsigned width;
        unsigned first_source;
        unsigned second_source;
        unsigned second_destination;
    };
    constexpr std::array cases{
        Case{1, 0, 32, 8, 24, 8},
        Case{2, 1, 16, 8, 24, 8},
        Case{3, 2, 8, 8, 24, 8},
        Case{1, 2, 16, 0, 8, 16},
    };
    for (const auto& item : cases) {
        TextureCommands commands;
        commands.image(item.image_size);
        commands.tile(item.tile_size, 0, 0);
        commands.bounds(0x34, 0, 0, (item.width - 1U) * 4U, 0);
        commands.run();
        commands.word(0, item.first_source);
        commands.word(item.second_destination, item.second_source);
        commands.check();
    }
}

TEST(rdp_texture_smaller_image_sizes_leave_gaps_in_split_banks) {
    for (unsigned size = 1; size <= 2; ++size) {
        TextureCommands commands;
        const unsigned pixels_per_word = 16U >> size;
        commands.image(size);
        commands.tile(3, 0, 0);
        commands.bounds(0x34, 0, 0, (pixels_per_word * 2U - 1U) * 4U, 0);
        commands.run();
        commands.split_word(0, 0);
        commands.split_word(pixels_per_word * 2U, 8);
        commands.check();
    }
}

TEST(rdp_texture_block_rounds_partial_words_and_adds_stride_at_each_dxt_carry) {
    TextureCommands commands;
    commands.image(1, 16);
    commands.tile(1, 8, 24);
    commands.bounds(0x33, 1, 0, 25, 0xfff);
    commands.run();
    commands.word(24, 1);
    commands.word(40, 9, 4);
    commands.word(64, 17, 4);
    commands.word(88, 25, 4);
    commands.check();
}

TEST(rdp_texture_block_larger_tile_size_retains_source_dxt_step) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(2, 8, 0);
    commands.bounds(0x33, 0, 0, 23, 0x800);
    commands.run();
    commands.word(0, 0);
    commands.word(24, 8, 4);
    commands.word(48, 16);
    commands.check();
}

TEST(rdp_texture_block_yuv_dxt_swaps_paired_bank_words) {
    TextureCommands commands;
    commands.image(2);
    commands.tile(2, 8, 0, 0, 1);
    commands.bounds(0x33, 0, 0, 11, 0x800);
    commands.run();
    commands.split_word(0, 0, false, true);
    commands.split_word(12, 8, true, true);
    commands.split_word(24, 16, false, true);
    commands.check();
}

TEST(rdp_texture_tlut_destination_size_controls_spacing) {
    for (unsigned size = 0; size <= 2; ++size) {
        TextureCommands commands;
        commands.image(2);
        commands.tile(size, 0, 0x800);
        commands.bounds(0x30, 0, 0, 8, 0);
        commands.run();
        const unsigned spacing = 8U << size;
        commands.palette_word(0x800, 0);
        commands.palette_word(0x800 + spacing, 2);
        commands.palette_word(0x800 + spacing * 2U, 4);
        commands.check();
    }
}

TEST(rdp_texture_tlut_8bit_sources_step_whole_words) {
    for (unsigned size = 0; size <= 2; ++size) {
        TextureCommands commands;
        commands.image(1);
        commands.tile(size, 0, 0x800);
        commands.bounds(0x30, 0, 0, 32, 0);
        commands.run();
        commands.palette_word(0x800, 0);
        commands.palette_word(0x800 + (16U << size), 8);
        commands.check();
    }
}

TEST(rdp_texture_tlut_32bit_sources_select_last_mirrored_word) {
    for (unsigned size = 0; size <= 2; ++size) {
        TextureCommands commands;
        commands.image(3);
        commands.tile(size, 0, 0x800);
        commands.bounds(0x30, 0, 0, 20, 0);
        commands.run();
        if (size == 0) {
            commands.palette_word(0x800, 8);
            commands.palette_word(0x808, 16);
        } else {
            const unsigned spacing = size == 1 ? 8U : 16U;
            commands.palette_word(0x800, 0);
            commands.palette_word(0x800 + spacing, 8);
            commands.palette_word(0x800 + spacing * 2U, 16);
        }
        commands.check();
    }
}

TEST(rdp_texture_empty_loads_preserve_tmem_and_tile_bounds) {
    for (unsigned opcode : {0x30U, 0x33U, 0x34U}) {
        TextureCommands commands;
        commands.image(1);
        commands.tile(1, 8, 0);
        commands.bounds(0x34, 0, 0, 0, 0);
        commands.bounds(opcode, opcode == 0x33 ? 1U : 4U, 0, 0, 0);
        commands.run();
        commands.word(0, 0);
        commands.check();
        CHECK_EQ(commands.system->bus.rdp.tile(0).s_low, 0U);
    }
}

TEST(rdp_texture_reversed_vertical_bounds_and_oversized_blocks_do_not_load) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(1, 8, 0);
    commands.bounds(0x34, 0, 4, 28, 0);
    commands.bounds(0x33, 0, 0, 2048, 0);
    commands.run();
    commands.check();
    CHECK_EQ(commands.system->bus.rdp.tile(0).s_high, 0U);
}

TEST(rdp_texture_four_bit_source_crashes_dp_and_prevents_later_fullsync) {
    for (unsigned opcode : {0x30U, 0x33U, 0x34U}) {
        TextureCommands commands;
        commands.image(0);
        commands.tile(0, 8, 0);
        commands.bounds(opcode, 0, 0, 0, 0);
        const u32 crash_end = commands.end;
        commands.append(0x29);
        commands.run();
        auto& rdp = commands.system->bus.rdp;
        CHECK_EQ(rdp.current(), crash_end);
        CHECK_EQ(rdp.read_register(0x0c) & 0x62U, 0x62U);
        CHECK_EQ(commands.system->bus.read(0x04300008, 4), 0ULL);
        rdp.write_register(0x0c, (1U << 2U) | (1U << 7U) | (1U << 8U));
        CHECK_EQ(rdp.current(), crash_end);
        CHECK_EQ(rdp.read_register(0x0c) & 0x62U, 0x62U);
        commands.check();
    }
}

TEST(rdp_texture_multiline_palette_load_crashes_even_when_horizontal_range_is_empty) {
    TextureCommands commands;
    commands.image(2);
    commands.tile(0, 0, 0x800);
    commands.bounds(0x30, 4, 0, 0, 4);
    commands.append(0x29);
    commands.run();
    CHECK_EQ(commands.system->bus.rdp.current(), commands.end - 8U);
    CHECK_EQ(commands.system->bus.rdp.read_register(0x0c) & 0x62U, 0x62U);
    commands.check();
}

TEST(rdp_texture_freeze_and_partial_commands_delay_loads_until_complete) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(1, 8, 0);
    commands.bounds(0x34, 0, 0, 0, 0);
    auto& rdp = commands.system->bus.rdp;
    rdp.write_register(0x0c, 1U << 3U);
    commands.run();
    commands.check();
    rdp.write_register(0x0c, 1U << 2U);
    commands.word(0, 0);
    commands.check();

    // A following load cannot execute as the second word of a texture rectangle.
    commands.append(0x24);
    rdp.write_register(4, commands.end);
    commands.bounds(0x34, 32, 0, 32, 0);
    rdp.write_register(4, commands.end);
    commands.check();
    commands.bounds(0x34, 32, 0, 32, 0);
    rdp.write_register(4, commands.end);
    commands.word(0, 8);
    commands.check();
}

TEST(rdp_texture_xbus_commands_still_load_texels_from_rdram) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(1, 8, 0);
    commands.bounds(0x34, 0, 0, 0, 0);
    for (u32 address = 0x1000; address < commands.end; ++address)
        commands.system->rsp.memory[address & 0xfffU] =
            static_cast<u8>(commands.system->bus.memory.read(address, 1));
    commands.system->bus.rdp.write_register(0x0c, 1U << 1U);
    commands.run();
    commands.word(0, 0);
    commands.check();
}

TEST(rdp_texture_reset_clears_memory_descriptors_and_crash_state) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(1, 8, 0, 3);
    commands.bounds(0x34, 0, 0, 28, 4, 3);
    commands.image(0);
    commands.bounds(0x34, 0, 0, 0, 0, 3);
    commands.run();
    auto& rdp = commands.system->bus.rdp;
    rdp.reset();
    commands.check();
    CHECK_EQ(rdp.tile(3).line_stride, 0U);
    CHECK_EQ(rdp.tile(3).size, 0U);
    CHECK_EQ(rdp.tile(3).t_high, 0U);
    CHECK_EQ(rdp.read_register(0x0c), 1U << 7U);
}

TEST(rdp_texture_sequential_loads_share_tmem_and_complete_before_fullsync) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(1, 8, 0);
    commands.bounds(0x34, 0, 0, 0, 0);
    commands.append(0x26);
    commands.append(0x28);
    commands.image(1, 16, 8);
    commands.tile(1, 8, 8, 7);
    commands.bounds(0x34, 0, 0, 0, 0, 7);
    commands.append(0x27);
    commands.image(1, 16, 16);
    commands.bounds(0x34, 0, 0, 0, 0);
    commands.append(0x29);
    commands.run();
    commands.word(0, 16);
    commands.word(8, 8);
    commands.check();
    CHECK_EQ(commands.system->bus.read(0x04300008, 4), 1ULL << 5U);
    CHECK_EQ(commands.system->bus.rdp.current(), commands.end);
    CHECK_EQ(commands.system->bus.rdp.read_register(0x0c) & 0x60U, 0U);
}

TEST(rdp_texture_loads_wrap_tmem_after_many_rows_without_restarting_source) {
    TextureCommands commands;
    commands.image(1, 16);
    commands.tile(1, 8, 0);
    commands.bounds(0x34, 0, 0, 0, 4095);
    commands.run();
    for (unsigned row = 512; row < 1024; ++row)
        commands.word(row * 8U, row * 16U, (row & 1U) * 4U);
    commands.check();
}

TEST(rdp_texture_rgba32_full_tmem_load_preserves_all_four_byte_lanes) {
    TextureCommands commands;
    commands.image(3, 1024);
    commands.tile(3, 0, 0);
    commands.bounds(0x34, 0, 0, 4092, 0);
    commands.run();
    for (unsigned word = 0; word < 512; ++word)
        commands.split_word(word * 4U, word * 8U);
    commands.check();
}

TEST(rdp_texture_horizontal_load_count_wraps_at_twelve_bits) {
    TextureCommands commands;
    commands.image(1);
    commands.tile(1, 0, 0);
    commands.bounds(0x34, 8, 0, 0, 0);
    commands.run();
    for (unsigned word = 0; word < 512; ++word)
        commands.word(word * 8U, 2U + word * 8U);
    commands.check();
}

TEST(rdp_texture_halt_after_fullsync_restores_busy_bits_and_preserves_pending_interrupt) {
    for (bool palette : {false, true}) {
        TextureCommands commands;
        commands.append(0x29);
        commands.image(palette ? 2U : 0U);
        commands.tile(0, 0, 0);
        commands.bounds(palette ? 0x30U : 0x34U, 0, 0, 0, palette ? 4U : 0U);
        const u32 halt_end = commands.end;
        commands.append(0x29);
        commands.run();
        CHECK_EQ(commands.system->bus.rdp.current(), halt_end);
        CHECK_EQ(commands.system->bus.rdp.read_register(0x0c) & 0x62U, 0x62U);
        CHECK_EQ(commands.system->bus.read(0x04300008, 4), 1ULL << 5U);
        commands.check();
    }
}
