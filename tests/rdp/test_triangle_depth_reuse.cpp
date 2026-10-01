#include "triangle_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {

constexpr u32 color_base = 0x8000U;

void prepare_scene(TriangleCommands& commands, unsigned size, unsigned depth_mode, unsigned coverage,
                   unsigned stored_z, bool alias, bool defer_depth) {
    const u32 depth_base = alias ? color_base : 0x8800U;
    commands.append(0x3f, (static_cast<u64>(size) << 51U) | (15ULL << 32U) | color_base);
    commands.append(0x3e, depth_base);
    commands.append(0x39, 0);
    const CombineCycle combine{.a = 1, .b = 8, .c = 4, .d = 7, .aa = 1, .ab = 7, .ac = 4, .ad = 7};
    commands.append(0x3c, combine_word(combine, combine));
    commands.modes(0x78U | (static_cast<u64>(depth_mode) << 10U) | static_cast<u64>(defer_depth));
    auto& memory = commands.system->bus.memory;
    for (unsigned pixel = 0; pixel < 64U; ++pixel) {
        if (size == 2U)
            memory.write_halfword(color_base + pixel * 2U, {static_cast<u16>(0xa54aU | (coverage >> 2U)),
                                                            static_cast<u8>(coverage & 3U)});
        else
            memory.write(color_base + pixel * 4U, 4U, 0xa55a7300U | (coverage << 5U));
    }
    const auto packed_z = static_cast<u16>(rdp_compress_depth(stored_z) << 2U);
    for (unsigned pixel = 0; pixel < 64U; ++pixel)
        memory.write_halfword(depth_base + pixel * 2U, {packed_z, static_cast<u8>(pixel & 3U)});
    commands.triangle(15U, {.major = 0x8000, .upper = 0x38000, .lower = 0x38000});
    memory.invalidate_banks();
}

Rdram::BankAccessSummary draw(TriangleCommands& commands) {
    Rdram::BankAccessSummary summary;
    const Rdram::BankAccessScope scope(commands.system->bus.memory, summary);
    commands.run();
    return summary;
}

} // namespace

TEST(rdp_triangle_image_read_depth_reuse_preserves_aliases_and_bank_order) {
    unsigned compared = 0;
    for (const unsigned size : {2U, 3U}) {
        for (unsigned depth_mode = 0; depth_mode < 4U; ++depth_mode) {
            for (unsigned coverage = 0; coverage < 8U; ++coverage) {
                for (const unsigned stored_z : {0U, 0x1ffe0U, 0x20020U, 0x3ffffU}) {
                    for (const bool alias : {false, true}) {
                        TriangleCommands early;
                        TriangleCommands deferred;
                        prepare_scene(early, size, depth_mode, coverage, stored_z, alias, false);
                        prepare_scene(deferred, size, depth_mode, coverage, stored_z, alias, true);
                        // A zero alpha threshold admits every pixel through the ordinary depth path.
                        const auto actual = draw(early);
                        const auto expected = draw(deferred);
                        const auto actual_hidden = early.system->bus.memory.hidden_memory();
                        const auto expected_hidden = deferred.system->bus.memory.hidden_memory();
                        for (unsigned address = color_base; address < 0x9000U; ++address) {
                            CHECK_EQ(early.system->bus.rdram[address], deferred.system->bus.rdram[address]);
                            CHECK_EQ(actual_hidden[address >> 1U], expected_hidden[address >> 1U]);
                        }
                        for (unsigned bank = 0; bank < actual.banks.size(); ++bank) {
                            const auto& a = actual.banks[bank];
                            const auto& b = expected.banks[bank];
                            CHECK_EQ(a.first_row, b.first_row);
                            CHECK_EQ(a.last_row, b.last_row);
                            CHECK_EQ(a.visited, b.visited);
                            CHECK_EQ(a.changed_row, b.changed_row);
                            CHECK_EQ(a.dirty, b.dirty);
                            CHECK_EQ(a.last_access, b.last_access);
                        }
                        ++compared;
                    }
                }
            }
        }
    }
    CHECK_EQ(compared, 512U);
}
