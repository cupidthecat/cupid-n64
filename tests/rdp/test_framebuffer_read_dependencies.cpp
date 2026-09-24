#include "color_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {
constexpr u32 memory_color = 0x204060e0U;

void store_memory_color(ColorCommands& commands, u32 color = memory_color) {
    commands.system->bus.memory.write(commands.address(0), 4, color);
}
} // namespace

TEST(rdp_framebuffer_one_cycle_reads_memory_selected_as_final_pixel_color) {
    ColorCommands commands;
    store_memory_color(commands);
    commands.modes(1ULL << 30U); // P = memory.
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), memory_color);
}

TEST(rdp_framebuffer_two_cycle_first_stage_reads_memory_selected_as_pixel_color) {
    ColorCommands commands;
    store_memory_color(commands);
    commands.modes((1ULL << 52U) | (1ULL << 30U) | (2ULL << 22U) | (3ULL << 18U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x1f3e5de0U);
}

TEST(rdp_framebuffer_two_cycle_first_stage_reads_memory_selected_as_second_color) {
    ColorCommands commands;
    store_memory_color(commands);
    commands.modes((1ULL << 52U) | (3ULL << 26U) | (1ULL << 22U) | (3ULL << 18U));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x010203e0U);
}

TEST(rdp_framebuffer_two_cycle_final_stage_reads_memory_selected_as_pixel_color) {
    ColorCommands commands;
    store_memory_color(commands);
    commands.modes((1ULL << 52U) | (1ULL << 28U)); // Second-cycle P = memory.
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), memory_color);
}

TEST(rdp_framebuffer_two_cycle_first_stage_uses_full_memory_alpha_without_image_read) {
    ColorCommands commands;
    store_memory_color(commands, 0x10203020U);
    commands.append(0x2e, 0x4000U);
    commands.modes((1ULL << 52U) | (1ULL << 18U) | 4U); // First-cycle B = memory alpha.
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0xb0582ce0U);
}
