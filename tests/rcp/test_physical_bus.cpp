#include "cupid/system.hpp"
#include "test.hpp"

namespace {
using namespace cupid;
} // namespace

TEST(physical_bus_sp_memory_subword_and_doubleword_stores_use_rcp_word_lanes) {
    System system;
    auto& bus = system.bus;

    bus.write(0x04000000U, 4, 0xdeadbeefU);
    bus.write(0x04000001U, 1, 0x78U);
    CHECK_EQ(bus.read(0x04000000U, 4), 0x00780000U);

    bus.write(0x04000004U, 4, 0xdeadbeefU);
    bus.write(0x04000006U, 2, 0x5678U);
    CHECK_EQ(bus.read(0x04000004U, 4), 0x00005678U);

    bus.write(0x04000008U, 4, 0xdeadbeefU);
    bus.write(0x0400000cU, 4, 0xaabbccddU);
    bus.write(0x04000008U, 8, 0x123456789abcdef0ULL);
    CHECK_EQ(bus.read(0x04000008U, 4), 0x12345678U);
    CHECK_EQ(bus.read(0x0400000cU, 4), 0xaabbccddU);
}

TEST(physical_bus_sp_memory_and_register_windows_mirror_to_their_last_cycle) {
    System system;
    auto& bus = system.bus;

    bus.write(0x04000000U, 4, 0x12345678U);
    bus.write(0x04001000U, 4, 0xabcdef01U);
    CHECK_EQ(bus.read(0x0403e000U, 4), 0x12345678U);
    CHECK_EQ(bus.read(0x0403f000U, 4), 0xabcdef01U);

    CHECK_EQ(bus.read(0x04040010U, 4), 1U);
    CHECK_EQ(bus.read(0x0407fff0U, 4), 1U);

    bus.write(0x04080000U, 4, 0x00000abcU);
    CHECK_EQ(bus.read(0x04080000U, 4), 0x00000abcU);
    CHECK_EQ(bus.read(0x040bffe0U, 4), 0x00000abcU);
}

TEST(physical_bus_rcp_register_blocks_apply_their_documented_decode_masks) {
    System system;
    auto& bus = system.bus;

    bus.write(0x04100000U, 4, 0x00123458U);
    CHECK_EQ(bus.read(0x041fffe0U, 4), 0x00123458U);

    bus.write(0x044fffc8U, 4, 0x5a5U);
    CHECK_EQ(bus.read(0x04400008U, 4), 0x5a5U);

    bus.write(0x045fffe8U, 4, 1U);
    CHECK_EQ(bus.read(0x0450000cU, 4) & (1U << 25U), 1U << 25U);

    bus.write(0x046fffd4U, 4, 0xa5U);
    CHECK_EQ(bus.read(0x04600014U, 4), 0xa5U);

    bus.write(0x047fffe4U, 4, 0x12345678U);
    CHECK_EQ(bus.read(0x04700004U, 4), 0x12345678U);

    bus.write(0x048fffe0U, 4, 0x0012345fU);
    CHECK_EQ(bus.read(0x04800000U, 4), 0x00123458U);
}

TEST(physical_bus_supported_window_edges_do_not_fall_into_adjacent_stalls) {
    {
        System system;
        CHECK_EQ(system.bus.read(0x040bfffcU, 4), 0U);
        CHECK(!system.cpu.frozen);
        CHECK_EQ(system.bus.read(0x040c0000U, 4), 0U);
        CHECK(system.cpu.frozen);
    }
    {
        System system;
        CHECK_EQ(system.bus.read(0x048ffffcU, 4), 0U);
        CHECK(!system.cpu.frozen);
        CHECK_EQ(system.bus.read(0x04900000U, 4), 0U);
        CHECK(system.cpu.frozen);
    }
    {
        System system;
        CHECK_EQ(system.bus.read(0x7ffffffcU, 4), 0xfffcfffcU);
        CHECK(!system.cpu.frozen);
        CHECK_EQ(system.bus.read(0x80000000U, 4), 0U);
        CHECK(system.cpu.frozen);
    }
}

TEST(physical_bus_unsupported_doubleword_read_stalls_before_register_side_effects) {
    System system;
    auto& bus = system.bus;

    CHECK_EQ(bus.read(0x0404001cU, 8), 0U);
    CHECK(system.cpu.frozen);
    system.cpu.frozen = false;

    CHECK_EQ(bus.read(0x0404001cU, 4), 0U);
    CHECK_EQ(bus.read(0x0404001cU, 4), 1U);
}

TEST(physical_bus_rcp_doubleword_store_drives_only_the_high_word) {
    System system;
    auto& bus = system.bus;

    bus.write(0x04300000U, 8, (static_cast<u64>(0x00000112U) << 32U) | 0xffffffffU);
    CHECK_EQ(bus.read(0x04300000U, 4), 0x92U);
    CHECK_EQ(bus.read(0x04300004U, 4), 0x02020102U);
}

TEST(physical_bus_dps_only_decodes_the_first_four_test_registers) {
    System system;
    auto& bus = system.bus;

    bus.write(0x04200004U, 4, 1U);
    CHECK_EQ(bus.read(0x04200004U, 4), 1U);

    CHECK_EQ(bus.read(0x04200014U, 4), 0U);
    bus.write(0x04200014U, 4, 0U);
    CHECK_EQ(bus.read(0x04200004U, 4), 1U);
}

TEST(physical_bus_unmapped_stalls_preserve_mi_register_state) {
    for (bool write : {false, true}) {
        System system;
        auto& bus = system.bus;
        bus.write(0x04300000U, 4, 0x155U | (1U << 8U));
        bus.write(0x0430000cU, 4, (1U << 1U) | (1U << 5U) | (1U << 9U));
        const u32 mode = static_cast<u32>(bus.read(0x04300000U, 4));
        const u32 mask = static_cast<u32>(bus.read(0x0430000cU, 4));

        if (write)
            bus.write(0x04900000U, 4, 0xdeadbeefU);
        else
            CHECK_EQ(bus.read(0x04900000U, 4), 0U);
        CHECK(system.cpu.frozen);
        CHECK_EQ(bus.read(0x04300000U, 4), mode);
        CHECK_EQ(bus.read(0x0430000cU, 4), mask);
    }
}
