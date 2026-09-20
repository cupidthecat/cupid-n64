#include "triangle_commands.hpp"

#include <algorithm>
#include <array>

using namespace cupid;
using namespace test::rdp;

namespace {
void noise_combiner(ColorCommands& commands) {
    commands.append(0x3a, (128ULL << 32U) | 0xffffffffU);
    commands.append(0x3c, combine_word({}, {.a = 7, .c = 14, .d = 7}));
}

std::array<u32, 64> pixels(const ColorCommands& commands) {
    std::array<u32, 64> result{};
    for (unsigned i = 0; i < result.size(); ++i)
        result[i] = commands.pixel(i % 8U, i / 8U);
    return result;
}
} // namespace

TEST(rdp_rectangle_noise_uses_quantized_combiner_values) {
    ColorCommands commands;
    noise_combiner(commands);
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    std::array<bool, 7> seen{};
    constexpr std::array<u32, 7> colors{0, 16, 48, 80, 112, 144, 176};
    for (const auto pixel : pixels(commands)) {
        const u32 color = pixel >> 24U;
        CHECK_EQ(pixel, (color * 0x01010100U) | 0xe0U);
        const auto found = std::find(colors.begin(), colors.end(), color);
        CHECK(found != colors.end());
        seen[static_cast<unsigned>(found - colors.begin())] = true;
    }
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
}

TEST(rdp_random_rgb_dither_varies_each_channel) {
    for (unsigned size : {2U, 3U}) {
        ColorCommands commands(size);
        commands.append(0x3a, 0x0b0b0bff);
        commands.append(0x2f, (2ULL << 38U) | (3ULL << 36U));
        commands.rectangle(0, 0, 32, 32);
        commands.run();
        std::array<bool, 8> seen{};
        for (const auto pixel : pixels(commands)) {
            unsigned mask = 0;
            for (unsigned channel = 0; channel < 3; ++channel) {
                const unsigned shift = size == 2U ? 11U - channel * 5U : 24U - channel * 8U;
                const unsigned value = (pixel >> shift) & (size == 2U ? 31U : 255U);
                CHECK(value == (size == 2U ? 1U : 11U) || value == (size == 2U ? 2U : 16U));
                mask |= static_cast<unsigned>(value == (size == 2U ? 2U : 16U)) << channel;
            }
            seen[mask] = true;
        }
        CHECK(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
    }
}

TEST(rdp_random_alpha_dither_affects_alpha_comparison) {
    ColorCommands commands;
    commands.append(0x3a, 0x8040207d);
    commands.append(0x39, 128);
    commands.append(0x2f, (3ULL << 38U) | (2ULL << 36U) | 1U);
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    const auto result = pixels(commands);
    const auto accepted = std::count(result.begin(), result.end(), 0x804020e0U);
    CHECK(accepted > 0);
    CHECK(accepted < 64);
    CHECK_EQ(accepted + std::count(result.begin(), result.end(), 0U), 64);
}

TEST(rdp_random_alpha_threshold_ignores_blend_alpha) {
    std::array<u32, 64> first{};
    for (unsigned alpha : {0U, 255U}) {
        ColorCommands commands;
        commands.append(0x3a, 0x80402080);
        commands.append(0x39, alpha);
        commands.modes(3U);
        commands.rectangle(0, 0, 32, 32);
        commands.run();
        const auto result = pixels(commands);
        const auto rejected = std::count(result.begin(), result.end(), 0U);
        CHECK(rejected > 0);
        CHECK(rejected < 64);
        if (alpha == 0)
            first = result;
        else
            CHECK_EQ(result, first);
    }
}

TEST(rdp_repeated_draws_resample_noise_and_reset_reproduces_it) {
    ColorCommands commands;
    noise_combiner(commands);
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    const auto first = pixels(commands);
    commands.end = 0x1000;
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    CHECK(pixels(commands) != first);
    commands.system->bus.rdp.reset();
    commands.end = 0x1000;
    commands.append(0x3f, (3ULL << 51U) | (7ULL << 32U) | 0x8000U);
    commands.modes();
    noise_combiner(commands);
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    CHECK_EQ(pixels(commands), first);
}

TEST(rdp_triangle_noise_reaches_the_combiner) {
    TriangleCommands commands;
    noise_combiner(commands);
    commands.triangle(8, {.middle = 32, .bottom = 32, .upper = 0x80000, .lower = 0x80000});
    commands.run();
    bool nonzero = false;
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            nonzero = nonzero || (commands.pixel(x, y) >> 8U) != 0;
    CHECK(nonzero);
}

TEST(rdp_noise_scissor_and_fields_preserve_samples_at_retained_pixels) {
    ColorCommands full;
    noise_combiner(full);
    full.rectangle(0, 0, 32, 32);
    full.run();
    for (bool field : {false, true}) {
        ColorCommands clipped;
        noise_combiner(clipped);
        clipped.append(0x2d, (8ULL << 44U) | (8ULL << 32U) | (static_cast<u64>(field) << 25U) |
                                 (24ULL << 12U) | 24U);
        clipped.rectangle(0, 0, 32, 32);
        clipped.run();
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                CHECK_EQ(clipped.pixel(x, y),
                         x >= 2U && x < 6U && y >= 2U && y < 6U && (!field || (y & 1U) == 0)
                             ? full.pixel(x, y)
                             : 0U);
    }
}

TEST(rdp_random_alpha_selection_requires_alpha_compare_enable) {
    ColorCommands commands;
    commands.append(0x3a, 0x80402000);
    commands.append(0x39, 255);
    commands.modes(2U);
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    for (const auto pixel : pixels(commands))
        CHECK_EQ(pixel, 0x804020e0U);
}

TEST(rdp_random_alpha_two_cycle_comparison_uses_first_cycle_alpha) {
    std::array<u32, 64> single{};
    for (bool two_cycles : {false, true}) {
        ColorCommands commands;
        commands.append(0x3a, 0x80402080);
        commands.append(0x3b, 0x80402000);
        commands.append(0x3c, combine_word({}, {.ad = two_cycles ? 5U : 3U}));
        commands.modes((static_cast<u64>(two_cycles) << 52U) | 3U);
        commands.rectangle(0, 0, 32, 32);
        commands.run();
        if (two_cycles)
            CHECK_EQ(pixels(commands), single);
        else
            single = pixels(commands);
    }
}

TEST(rdp_noise_does_not_reset_at_command_buffer_boundaries_or_sync) {
    std::array<u32, 64> whole{};
    for (bool split : {false, true}) {
        ColorCommands commands;
        noise_combiner(commands);
        commands.rectangle(0, 0, 32, 32);
        if (split) {
            commands.run();
            commands.end = 0x1000;
        }
        commands.append(0x27, 0);
        commands.rectangle(0, 0, 32, 32);
        commands.run();
        if (split)
            CHECK_EQ(pixels(commands), whole);
        else
            whole = pixels(commands);
    }
}

TEST(rdp_random_rgb_dither_saturates_without_wrapping) {
    ColorCommands commands;
    commands.append(0x3a, 0xfdffffff);
    commands.append(0x2f, (2ULL << 38U) | (3ULL << 36U));
    commands.rectangle(0, 0, 32, 32);
    commands.run();
    bool rounded = false;
    for (const auto pixel : pixels(commands)) {
        CHECK(pixel == 0xfdffffe0U || pixel == 0xffffffe0U);
        rounded = rounded || pixel == 0xffffffe0U;
    }
    CHECK(rounded);
}

TEST(rdp_random_alpha_rejection_preserves_depth_and_hidden_bits) {
    for (bool two_cycles : {false, true}) {
        ColorCommands commands;
        auto& memory = commands.system->bus.memory;
        for (unsigned pixel = 0; pixel < 64; ++pixel) {
            memory.write(0x9000U + pixel * 2U, 2, 0xbeef);
            memory.set_hidden_pair(0x9000U + pixel * 2U, 2);
        }
        commands.append(0x3e, 0x9000);
        commands.append(0x2e, 0x20000001);
        commands.append(0x3a, 0x80402080);
        commands.modes((static_cast<u64>(two_cycles) << 52U) | 3U | 4U | 32U);
        commands.rectangle(0, 0, 32, 32);
        commands.run();
        unsigned accepted = 0;
        for (unsigned pixel = 0; pixel < 64; ++pixel) {
            const bool written = commands.pixel(pixel % 8U, pixel / 8U) != 0;
            accepted += static_cast<unsigned>(written);
            CHECK_EQ(memory.read(0x9000U + pixel * 2U, 2), written ? 0x1000U : 0xbeefU);
            CHECK_EQ(memory.hidden_pair(0x9000U + pixel * 2U), written ? 0U : 2U);
        }
        CHECK(accepted > 0 && accepted < 64);
    }
}

TEST(rdp_noise_partial_triangle_packets_do_not_advance_the_sequence) {
    for (unsigned opcode = 8; opcode < 16; ++opcode) {
        std::array<u32, 8> whole{};
        for (bool split : {false, true}) {
            TriangleCommands commands;
            noise_combiner(commands);
            commands.triangle(opcode);
            commands.system->bus.rdp.write_register(0, 0x1000);
            if (split) {
                commands.system->bus.rdp.write_register(4, commands.end - 8U);
                CHECK_EQ(commands.pixel(), 0U);
            }
            commands.system->bus.rdp.write_register(4, commands.end);
            for (unsigned pixel = 0; pixel < 8; ++pixel) {
                const u32 value = commands.pixel(pixel % 4U, pixel / 4U);
                if (split)
                    CHECK_EQ(value, whole[pixel]);
                else
                    whole[pixel] = value;
            }
        }
    }
}
