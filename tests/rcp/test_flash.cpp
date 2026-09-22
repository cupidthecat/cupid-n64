#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>

namespace {
using namespace cupid;

void command(Bus& bus, u32 value) {
    bus.write(0x08010000, 4, value);
    bus.tick(140);
}

void read_flash(Bus& bus, u32 address, u32 bytes) {
    bus.write(0x0460002c, 4, 5);
    bus.write(0x04600000, 4, 0x2000);
    bus.write(0x04600004, 4, address);
    bus.write(0x0460000c, 4, bytes - 1);
    bus.tick(10000);
    bus.write(0x04600010, 4, 2);
}
} // namespace

TEST(flash_silicon_id_contains_the_device_header_and_ids) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    command(bus, 0xe1000000);
    read_flash(bus, 0x08000000, 8);
    CHECK_EQ(bus.read(0x2000, 4), 0x11118001U);
    CHECK_EQ(bus.read(0x2004, 4), 0x00c2001eU);
}

TEST(flash_status_reports_the_write_state_machine) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    command(bus, 0xd2000000);
    command(bus, 0xd2000000);
    static_cast<void>(bus.read(0x08000000, 4));
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
    command(bus, 0xa5000000);
    static_cast<void>(bus.read(0x08000000, 4));
    CHECK_EQ(bus.read(0x08000000, 4), 0x000d000dU);
    bus.tick(218750 - 141);
    CHECK_EQ(bus.read(0x08000000, 4), 0x000d000dU);
    bus.tick(1);
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
}

TEST(flash_programming_rejects_mode_changes_while_busy) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    bus.flashram[0] = 0x12;
    bus.flashram[1] = 0x34;
    command(bus, 0xa5000001);
    command(bus, 0xf0000000);
    static_cast<void>(bus.read(0x08000000, 4));
    CHECK_EQ(bus.read(0x08000000, 4), 0x000d000dU);
    bus.tick(218750);
    command(bus, 0xf0000000);
    CHECK_EQ(bus.read(0x08000000, 2), 0x1234U);
}

TEST(flash_word_indexed_array_reads_advance_by_halfwords) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    write_be32(bus.flashram.data() + 0x80, 0x12345678);
    CHECK_EQ(bus.read(0x08000040, 4), 0x12345678U);
}

TEST(flash_status_requires_two_consecutive_commands_and_returns_one_stale_halfword) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    write_be32(bus.flashram.data(), 0x12345678);
    command(bus, 0xd2000000);
    CHECK_EQ(bus.read(0x08000000, 4), 0x12345678U);
    command(bus, 0xf0000000);
    command(bus, 0xd2000000);
    CHECK_EQ(bus.read(0x08000000, 4), 0x12345678U);
    command(bus, 0xd2000000);
    CHECK_EQ(bus.read(0x08000000, 4), 0x5678008cU);
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
}

TEST(flash_page_buffer_is_readable_and_programming_only_clears_array_bits) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    write_be32(bus.flashram.data() + 128, 0xfff000ff);
    command(bus, 0xb4000000);
    bus.write(0x08000000, 4, 0x0ff055aa);
    bus.tick(140);
    CHECK_EQ(bus.read(0x08000000, 4), 0x0ff055aaU);
    command(bus, 0xa5000001);
    bus.tick(218750);
    CHECK_EQ(read_be32(bus.flashram.data() + 128), 0x0ff000aaU);
    command(bus, 0xb4000000);
    CHECK_EQ(bus.read(0x08000000, 4), 0xffffffffU);
    command(bus, 0xa5000001);
    bus.tick(218750);
    CHECK_EQ(read_be32(bus.flashram.data() + 128), 0x0ff000aaU);
}

TEST(flash_sector_erase_requires_setup_and_preserves_other_sectors) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    std::fill(bus.flashram.begin(), bus.flashram.end(), u8{0x12});
    command(bus, 0x78000000);
    CHECK_EQ(bus.read(0x08000000, 4), 0x12121212U);
    command(bus, 0x4b000080);
    command(bus, 0x78000000);
    for (u32 offset = 0; offset < bus.flashram.size(); ++offset)
        CHECK_EQ(bus.flashram[offset], offset >= 0x4000 && offset < 0x8000 ? 0xffU : 0x12U);
    static_cast<void>(bus.read(0x08000000, 4));
    CHECK_EQ(bus.read(0x08000000, 4), 0x000e000eU);
    bus.tick(5312500 - 141);
    CHECK_EQ(bus.read(0x08000000, 4), 0x000e000eU);
    bus.tick(1);
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
    CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0U);
}

TEST(flash_chip_erase_clears_the_whole_array) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    std::fill(bus.flashram.begin(), bus.flashram.end(), u8{0});
    command(bus, 0x3c000000);
    command(bus, 0x78000000);
    bus.tick(5312500);
    for (const auto byte : bus.flashram)
        CHECK_EQ(byte, 0xffU);
    static_cast<void>(bus.read(0x08000000, 4));
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
}

TEST(flash_commands_use_the_same_halfword_path_for_cpu_stores_and_dma) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    bus.write(0x3000, 4, 0xe1000000);
    bus.write(0x04600000, 4, 0x3000);
    bus.write(0x04600004, 4, 0x08010000);
    bus.write(0x04600008, 4, 3);
    bus.tick(1000);
    bus.write(0x04600010, 4, 2);
    CHECK_EQ(bus.read(0x08000000, 4), 0x11118001U);
    command(bus, 0xf0000000);
    bus.write(0x08010004, 4, 0xe1000000);
    bus.tick(140);
    CHECK_EQ(bus.read(0x08000000, 4), 0xffffffffU);
}

TEST(flash_status_open_bus_persists_until_a_command) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    command(bus, 0xd2000000);
    command(bus, 0xd2000000);
    CHECK_EQ(bus.read(0x08021234, 4), 0x12341234U);
    CHECK_EQ(bus.read(0x08001230, 4), 0x12301230U);
    command(bus, 0xe1000000);
    CHECK_EQ(bus.read(0x08001230, 4), 0x11118001U);
}

TEST(flash_array_bursts_wrap_the_internal_address_counter) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    write_be32(bus.flashram.data(), 0x12345678);
    write_be32(bus.flashram.data() + 0x7ffc, 0xabcdef01);
    write_be32(bus.flashram.data() + 0x8000, 0x3456789a);
    bus.write(0x0460002c, 4, 15);
    bus.write(0x04600000, 4, 0x2000);
    bus.write(0x04600004, 4, 0x08003ffe);
    bus.write(0x0460000c, 4, 7);
    bus.tick(1000);
    CHECK_EQ(bus.read(0x2000, 4), 0xabcdef01U);
    CHECK_EQ(bus.read(0x2004, 4), 0x12345678U);
}

TEST(flash_reset_cancels_busy_state_without_erasing_saved_data) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);
    command(bus, 0xb4000000);
    bus.write(0x08000000, 4, 0x12345678);
    bus.tick(140);
    command(bus, 0xa5000000);
    bus.reset();
    CHECK_EQ(bus.read(0x08000000, 4), 0x12345678U);
    command(bus, 0xd2000000);
    command(bus, 0xd2000000);
    static_cast<void>(bus.read(0x08000000, 4));
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
    bus.tick(218750);
    CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
}

TEST(flash_completion_uses_rcp_cycles_during_cpu_advancement) {
    for (bool split : {false, true}) {
        System system;
        auto& bus = system.bus;
        bus.set_save_type(SaveType::FlashRam);
        bus.write(0x08010000, 4, 0xa5000000);
        if (split) {
            for (unsigned step = 0; step < 4; ++step)
                system.advance(82031);
        } else {
            system.advance(328124);
        }
        static_cast<void>(bus.read(0x08000000, 4));
        CHECK_EQ(bus.read(0x08000000, 4), 0x000d000dU);
        system.advance(1);
        CHECK_EQ(bus.read(0x08000000, 4), 0x008c008cU);
    }
}
