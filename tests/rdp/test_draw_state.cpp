#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

constexpr std::array<unsigned, 11> primitives{0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                                              0x0e, 0x0f, 0x24, 0x25, 0x36};
constexpr std::array<unsigned, 11> lengths{32, 48, 96, 112, 96, 112, 160, 176, 16, 16, 8};

struct DrawCommands {
    std::unique_ptr<System> system = std::make_unique<System>();
    u32 end = 0x1000;
    DrawCommands(unsigned size, unsigned cycle, u64 modes) {
        test::initialize_memory(*system);
        append(0x3f, (static_cast<u64>(size) << 51U) | 0x8000U);
        append(0x2f, (static_cast<u64>(cycle) << 52U) | modes);
        append(0x2d, 0); // Empty geometry still validates the draw state.
        append(0x29, 0);
    }
    void append(unsigned opcode, u64 payload) {
        system->bus.write(end, 8, (static_cast<u64>(opcode) << 56U) | payload);
        end += 8;
    }
    void primitive(unsigned index) {
        append(primitives[index], 0);
        for (unsigned byte = 8; byte < lengths[index]; byte += 8)
            append(0, 0);
    }
    void run() {
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
    }
};
} // namespace

TEST(rdp_draw_invalid_fill_and_copy_states_halt_all_primitive_opcodes) {
    for (unsigned state = 0; state < 5; ++state) {
        const unsigned size = state == 0 ? 0U : state == 4 ? 3U : 2U;
        const unsigned cycle = state == 4 ? 2U : 3U;
        const u64 modes = state == 1 ? 0x10U : state == 2 ? 0x40U : state == 3 ? 0x20U : 0U;
        for (unsigned index = 0; index < primitives.size(); ++index) {
            DrawCommands c(size, cycle, modes);
            c.primitive(index);
            const u32 halted_at = c.end;
            c.append(0x29, 0);
            c.run();
            CHECK_EQ(c.system->bus.rdp.current(), halted_at);
            CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0x62U);
            c.system->bus.rdp.write_register(12, 0x1c4U);
            CHECK_EQ(c.system->bus.rdp.current(), halted_at);
            CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0x62U);
        }
    }
}

TEST(rdp_draw_state_is_checked_only_after_the_entire_primitive_arrives) {
    for (unsigned index = 0; index < primitives.size() - 1U; ++index) {
        DrawCommands c(0, 3, 0);
        c.primitive(index);
        c.system->bus.rdp.write_register(0, 0x1000);
        c.system->bus.rdp.write_register(4, c.end - 8U);
        CHECK_EQ(c.system->bus.rdp.read_register(12) & 2U, 0U);
        c.system->bus.rdp.write_register(4, c.end);
        CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0x62U);
    }
}

TEST(rdp_draw_fill_depth_update_with_primitive_depth_is_allowed) {
    for (unsigned index = 0; index < primitives.size(); ++index) {
        DrawCommands c(2, 3, 0x24);
        c.primitive(index);
        c.append(0x29, 0);
        c.run();
        CHECK_EQ(c.system->bus.rdp.current(), c.end);
        CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0U);
    }
}

TEST(rdp_draw_invalid_state_without_primitive_does_not_halt) {
    DrawCommands c(0, 3, 0x70);
    c.run();
    CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0U);
}

TEST(rdp_draw_reset_recovers_halted_command_processing) {
    DrawCommands c(3, 2, 0);
    c.primitive(8);
    c.run();
    CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0x62U);
    c.system->bus.rdp.reset();
    const u32 start = c.end;
    c.append(0x29, 0);
    c.system->bus.rdp.write_register(0, start);
    c.system->bus.rdp.write_register(4, c.end);
    CHECK_EQ(c.system->bus.rdp.current(), c.end);
    CHECK_EQ(c.system->bus.rdp.read_register(12) & 0x62U, 0U);
}
