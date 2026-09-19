#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <memory>

namespace {

using namespace cupid;

std::unique_ptr<System> machine() {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    return system;
}

void write_command(Bus& bus, u32 address, u64 command) {
    bus.write(address, 8, command);
}

constexpr u64 command(unsigned opcode, u64 payload = 0) {
    return (static_cast<u64>(opcode & 0x3fU) << 56U) | (payload & 0x00ffffffffffffffULL);
}

} // namespace

TEST(rdp_bus_construction_and_reset_leave_dp_interrupt_clear) {
    System system;
    CHECK_EQ(system.bus.read(0x04300008U, 4), 0ULL);
    CHECK(!system.bus.interrupt_pending());
    system.bus.set_interrupt(5, true);
    CHECK_EQ(system.bus.read(0x04300008U, 4), 1ULL << 5U);
    CHECK(!system.bus.interrupt_pending());
    system.bus.write(0x0430000cU, 4, 1U << 11U);
    CHECK(system.bus.interrupt_pending());
    system.bus.reset();
    CHECK_EQ(system.bus.read(0x04300008U, 4), 0ULL);
    CHECK(!system.bus.interrupt_pending());
    CHECK_EQ(system.bus.rdp.read_register(0x0c), 1U << 7);
}

TEST(rdp_start_end_masking_and_start_valid_latch) {
    auto system = machine();
    auto& rdp = system->bus.rdp;
    rdp.write_register(0x0c, 1U << 3);
    rdp.write_register(0x00, 0x12ffffffU);
    CHECK_EQ(rdp.start(), 0x00fffff8U);
    CHECK((rdp.read_register(0x0c) & (1U << 10)) != 0);
    rdp.write_register(0x00, 0x00123450U);
    CHECK_EQ(rdp.start(), 0x00fffff8U);
    rdp.write_register(0x04, 0x00800007U);
    CHECK_EQ(rdp.end(), 0x00800000U);
    CHECK_EQ(rdp.current(), 0x00fffff8U);
    CHECK((rdp.read_register(0x0c) & (1U << 10)) == 0);
    CHECK((rdp.read_register(0x0c) & (1U << 9)) == 0);
}

TEST(rdp_status_control_pairs_and_dps_test_data_masks) {
    auto system = machine();
    auto& rdp = system->bus.rdp;
    rdp.write_register(0x0c, (1U << 1) | (1U << 3) | (1U << 5));
    CHECK((rdp.read_register(0x0c) & 0x7U) == 0x7U);
    rdp.write_register(0x0c, (1U << 0) | (1U << 2) | (1U << 4));
    CHECK((rdp.read_register(0x0c) & 0x7U) == 0);
    rdp.tick(123);
    CHECK_EQ(rdp.read_register(0x10), 123U);
    rdp.write_register(0x0c, 1U << 9);
    CHECK_EQ(rdp.read_register(0x10), 0U);

    rdp.write_test_register(0x08, 2);
    rdp.write_test_register(0x0c, 0xabcdef12U);
    CHECK_EQ(rdp.read_test_register(0x0c), 0x12U);
    rdp.write_test_register(0x08, 3);
    rdp.write_test_register(0x0c, ~0U);
    CHECK_EQ(rdp.read_test_register(0x0c), 0U);
    rdp.write_test_register(0x04, 1);
    CHECK_EQ(rdp.read_test_register(0x04), 1U);
}

TEST(rdp_freeze_defers_commands_until_clear) {
    auto system = machine();
    auto& bus = system->bus;
    auto& rdp = bus.rdp;
    bus.write(0x0430000c, 4, 1U << 11);
    write_command(bus, 0x1000, command(0x29));
    rdp.write_register(0x0c, 1U << 3);
    rdp.write_register(0x00, 0x1000);
    rdp.write_register(0x04, 0x1008);
    CHECK_EQ(rdp.current(), 0x1000U);
    CHECK(!bus.interrupt_pending());
    rdp.write_register(0x0c, 1U << 2);
    CHECK_EQ(rdp.current(), 0x1008U);
    CHECK(bus.interrupt_pending());
    CHECK((rdp.read_register(0x0c) & (1U << 3)) == 0);
}

TEST(rdp_partial_multiword_command_is_buffered_before_following_fullsync) {
    auto system = machine();
    auto& bus = system->bus;
    auto& rdp = bus.rdp;
    bus.write(0x0430000c, 4, 1U << 11);
    write_command(bus, 0x2000, command(0x24));
    write_command(bus, 0x2008, 0x0123456789abcdefULL);
    write_command(bus, 0x2010, command(0x29));
    rdp.write_register(0x00, 0x2000);
    rdp.write_register(0x04, 0x2008);
    CHECK_EQ(rdp.current(), 0x2008U);
    CHECK(!bus.interrupt_pending());
    rdp.write_register(0x04, 0x2018);
    CHECK_EQ(rdp.current(), 0x2018U);
    CHECK(bus.interrupt_pending());
}

TEST(rdp_xbus_wraps_dmem_while_current_tracks_unmasked_range) {
    auto system = machine();
    auto& bus = system->bus;
    auto& rdp = bus.rdp;
    bus.write(0x0430000c, 4, 1U << 11);
    const u64 nop = command(0x00);
    const u64 sync = command(0x29);
    for (unsigned byte = 0; byte < 8; ++byte) {
        system->rsp.memory[0xff8U + byte] = static_cast<u8>(nop >> ((7U - byte) * 8U));
        system->rsp.memory[byte] = static_cast<u8>(sync >> ((7U - byte) * 8U));
    }
    rdp.write_register(0x0c, 1U << 1);
    rdp.write_register(0x00, 0x0ff8);
    rdp.write_register(0x04, 0x1008);
    CHECK_EQ(rdp.current(), 0x1008U);
    CHECK((rdp.read_register(0x0c) & 1U) != 0);
    CHECK(bus.interrupt_pending());
}

TEST(rdp_fill_cycle_writes_rgba16_and_rgba32_framebuffers) {
    auto system = machine();
    auto& bus = system->bus;
    auto& rdp = bus.rdp;
    constexpr u32 stream = 0x3000;
    constexpr u32 framebuffer = 0x5000;
    constexpr unsigned width = 4;
    constexpr unsigned height = 3;
    const u64 set_other = command(0x2f, 3ULL << 52U);
    const u64 set_color16 =
        command(0x3f, (2ULL << 51U) | (static_cast<u64>(width - 1U) << 32U) | framebuffer);
    const u64 set_fill16 = command(0x37, 0x7c1f7c1fU);
    const u64 rectangle = command(0x36, (static_cast<u64>((width - 1U) << 2U) << 44U) |
                                            (static_cast<u64>((height - 1U) << 2U) << 32U));
    write_command(bus, stream + 0x00, set_color16);
    write_command(bus, stream + 0x08, set_other);
    write_command(bus, stream + 0x10, set_fill16);
    write_command(bus, stream + 0x18, rectangle);
    rdp.write_register(0x00, stream);
    rdp.write_register(0x04, stream + 0x20);
    for (unsigned pixel = 0; pixel < width * height; ++pixel) {
        CHECK_EQ(bus.memory.read(framebuffer + pixel * 2U, 2), 0x7c1fULL);
    }

    constexpr u32 framebuffer32 = 0x6000;
    const u64 set_color32 =
        command(0x3f, (3ULL << 51U) | (static_cast<u64>(width - 1U) << 32U) | framebuffer32);
    const u64 set_fill32 = command(0x37, 0x89abcdefU);
    write_command(bus, stream + 0x20, set_color32);
    write_command(bus, stream + 0x28, set_fill32);
    write_command(bus, stream + 0x30, rectangle);
    rdp.write_register(0x04, stream + 0x38);
    for (unsigned pixel = 0; pixel < width * height; ++pixel) {
        CHECK_EQ(bus.memory.read(framebuffer32 + pixel * 4U, 4), 0x89abcdefULL);
    }
}

TEST(rdp_unshaded_triangle_rasterizes_subpixel_coverage) {
    auto system = machine();
    auto& bus = system->bus;
    auto& rdp = bus.rdp;
    constexpr u32 stream = 0x3800;
    constexpr u32 framebuffer = 0x7000;
    constexpr unsigned width = 4;

    const u64 set_color = command(0x3f, (3ULL << 51U) | (static_cast<u64>(width - 1U) << 32U) | framebuffer);
    const u64 set_scissor = command(0x2d, (16ULL << 12U) | 4ULL);
    const u64 set_other = command(0x2f, 2ULL << 8U);
    const u64 set_blend = command(0x39, 0x00ff0000U);
    const u64 edge = command(0x08, (1ULL << 55U) | (4ULL << 32U));
    const u64 low = static_cast<u64>(4U << 16U) << 32U;
    const u64 high = 0;
    const u64 middle = static_cast<u64>(4U << 16U) << 32U;
    const u64 sync = command(0x29);

    write_command(bus, stream + 0x00, set_color);
    write_command(bus, stream + 0x08, set_scissor);
    write_command(bus, stream + 0x10, set_other);
    write_command(bus, stream + 0x18, set_blend);
    write_command(bus, stream + 0x20, edge);
    write_command(bus, stream + 0x28, low);
    write_command(bus, stream + 0x30, high);
    write_command(bus, stream + 0x38, middle);
    write_command(bus, stream + 0x40, sync);

    rdp.write_register(0x00, stream);
    rdp.write_register(0x04, stream + 0x48);
    for (unsigned x = 0; x < width; ++x) {
        CHECK_EQ(bus.memory.read(framebuffer + x * 4U, 4), 0xe0ff0000ULL);
    }
    CHECK_EQ(rdp.current(), stream + 0x48);
}
