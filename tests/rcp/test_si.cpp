#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

void start_dma(System& system, u32 address, bool read) {
    system.bus.write(0x04800000, 4, 0x2000);
    system.bus.write(read ? 0x04800004 : 0x04800010, 4, address);
    CHECK_EQ(system.bus.read(0x04800018, 4) & 1U, 1U);
}
} // namespace

TEST(si_dma_read_uses_the_programmed_pif_address_and_wraps_at_two_kibibytes) {
    for (u32 address : {0x1fc00000U, 0x1fc007c0U, 0x1fc00fc4U, 0x1fcffffeU}) {
        System system;
        test::initialize_memory(system);
        for (unsigned index = 0; index < system.bus.pif.size(); ++index)
            system.bus.pif[index] = static_cast<u8>((index >> 8) ^ index);
        system.bus.pif[0x7ff] = 0;
        const auto image = system.bus.pif;
        start_dma(system, address, true);
        system.bus.tick(13999);
        CHECK_EQ(system.bus.read(0x2000, 4), 0U);
        CHECK_EQ(system.bus.read(0x04800018, 4) & 0x1001U, 1U);
        system.bus.tick(1);
        for (u32 offset = 0; offset < 64; ++offset)
            CHECK_EQ(system.bus.read_ram_byte(0x2000 + offset), image[((address & ~3U) + offset) & 0x7ffU]);
        CHECK_EQ(system.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
        CHECK_EQ(system.bus.read(0x04300008, 4) & 2U, 2U);
        system.bus.write(0x04800018, 4, 0);
        CHECK_EQ(system.bus.read(0x04300008, 4) & 2U, 0U);
    }
}

TEST(si_dma_read_respects_rom_lockout_without_hiding_pif_ram) {
    System system;
    test::initialize_memory(system);
    system.bus.pif.fill(0xab);
    system.bus.pif[0x7ff] = 0;
    system.bus.write(0x1fc007fc, 4, 0x10);
    system.bus.tick(2150);
    system.bus.write(0x04800018, 4, 0);
    start_dma(system, 0x1fc007a0, true);
    system.bus.tick(14000);
    for (u32 index = 0; index < 64; ++index)
        CHECK_EQ(system.bus.read_ram_byte(0x2000 + index), index < 32 ? 0U : 0xabU);
}

TEST(si_dma_write_uses_the_programmed_address_and_keeps_boot_code_intact) {
    System system;
    test::initialize_memory(system);
    system.bus.pif.fill(0x5a);
    system.bus.pif[0x7ff] = 0;
    const auto before = system.bus.pif;
    for (u32 index = 0; index < 64; ++index)
        system.bus.write_ram_byte(0x2000 + index, static_cast<u8>(index + 1));
    // Keep the control byte clear; the transfer continues into the boot-ROM mirror.
    system.bus.write_ram_byte(0x2000 + 59, 0);
    start_dma(system, 0x1fc00fc4, false);
    system.bus.tick(4064);
    CHECK(system.bus.pif == before);
    system.bus.tick(1);
    CHECK_EQ(system.bus.pif[0x7c0], 0x5aU);
    CHECK_EQ(system.bus.pif[0x7c3], 0x5aU);
    for (u32 index = 0; index < 59; ++index)
        CHECK_EQ(system.bus.pif[0x7c4 + index], index + 1);
    CHECK_EQ(system.bus.pif[0x7ff], 0U);
    CHECK_EQ(system.bus.pif[0], 0x5aU);
    CHECK_EQ(system.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
}

TEST(si_pif_store_completes_on_the_serial_bus_clock) {
    System system;
    system.bus.write(0x1fc007c0, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x9b3U);
    CHECK_EQ(system.bus.pif[0x7c0], 0x12U);
    system.bus.tick(2149);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x9b3U);
    CHECK_EQ(system.bus.read(0x04300008, 4) & 2U, 0U);
    system.bus.tick(1);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x1000U);
    CHECK_EQ(system.bus.read(0x04300008, 4) & 2U, 2U);
    CHECK_EQ(system.bus.read(0x1fc007c0, 4), 0x12345678U);
    system.bus.write(0x04800018, 4, 0);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0U);
}

TEST(si_pif_store_while_io_busy_is_ignored_without_restarting_the_clock) {
    System system;
    system.bus.write(0x1fc007c0, 4, 0x12345678);
    system.bus.tick(1000);
    system.bus.write(0x1fc007c4, 4, 0xdeadbeef);
    system.bus.tick(1150);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x1000U);
    CHECK_EQ(system.bus.read(0x1fc007c0, 4), 0x12345678U);
    CHECK_EQ(system.bus.read(0x1fc007c4, 4), 0U);
}

TEST(si_pif_read_during_io_busy_returns_the_store_latch_once) {
    System system;
    system.bus.write(0x1fc007c0, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x1fc00fc5, 1), 0x34U);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x9b1U);
    CHECK_EQ(system.bus.read(0x1fc007c4, 4), 0U);
    system.bus.tick(2150);
    CHECK_EQ(system.bus.read(0x04300008, 4) & 2U, 0U);
    // A new transaction can start after the read releases the I/O latch.
    system.bus.write(0x1fc007c4, 4, 0xabcdef01);
    system.bus.tick(2150);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x1000U);
    CHECK_EQ(system.bus.read(0x1fc007c4, 4), 0xabcdef01U);
}

TEST(si_status_acknowledgement_does_not_cancel_a_pending_pif_store) {
    System system;
    system.bus.write(0x1fc007c1, 1, 0x78);
    system.bus.write(0x04800018, 4, 0);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x9b3U);
    system.bus.tick(2150);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0x1000U);
    CHECK_EQ(system.bus.read(0x1fc007c0, 4), 0x00780000U);
}

TEST(si_dma_status_reports_the_transfer_direction_and_clears_on_completion) {
    for (bool read : {false, true}) {
        System system;
        test::initialize_memory(system);
        start_dma(system, 0x1fc007c0, read);
        CHECK_EQ(system.bus.read(0x04800018, 4), read ? 0x141U : 0x411U);
        system.bus.tick(read ? 14000 : 4065);
        CHECK_EQ(system.bus.read(0x04800018, 4), 0x1000U);
    }
}

TEST(si_reset_cancels_pending_io_completion) {
    System system;
    system.bus.write(0x1fc007c0, 4, 0x12345678);
    system.reset();
    system.bus.tick(2150);
    CHECK_EQ(system.bus.read(0x04800018, 4), 0U);
    CHECK_EQ(system.bus.read(0x04300008, 4) & 2U, 0U);
}
