#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>

namespace {
using namespace cupid;

constexpr std::array<FlashChip, 7> chips{FlashChip::Mx29L0000,  FlashChip::Mx29L0001,  FlashChip::Mx29L1100,
                                         FlashChip::Mx29L1101A, FlashChip::Mx29L1101B, FlashChip::Mx29L1101C,
                                         FlashChip::Mn63F81Mpn};

struct FlashFixture {
    System system;
    Bus& bus{system.bus};

    explicit FlashFixture(FlashChip chip) {
        test::initialize_memory(system);
        bus.set_save_type(SaveType::FlashRam);
        bus.set_flash_chip(chip);
    }
    void command(u32 value) {
        bus.write(0x08010000, 4, value);
        bus.tick(140);
    }
    u32 status() {
        static_cast<void>(bus.read(0x08000000, 4));
        return static_cast<u32>(bus.read(0x08000000, 4));
    }
    void dma(u32 address, u32 bytes, u32 page = 15) {
        bus.write(0x0460002c, 4, page);
        bus.write(0x04600000, 4, 0x2000);
        bus.write(0x04600004, 4, address);
        bus.write(0x0460000c, 4, bytes - 1);
        bus.tick(10000);
        bus.write(0x04600010, 4, 2);
    }
};
} // namespace

TEST(flash_chips_identify_all_variants_and_preserve_burst_tail_behavior) {
    constexpr std::array<u32, 7> ids{0x00c20000, 0x00c20001, 0x00c2001e, 0x00c2001d,
                                     0x00c20084, 0x00c2008e, 0x003200f1};
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        fixture.command(0xe1000000);
        fixture.dma(0x08000000, 16);
        CHECK_EQ(fixture.bus.read(0x2000, 4), 0x11118001U);
        CHECK_EQ(fixture.bus.read(0x2004, 4), ids[index]);
        CHECK_EQ(fixture.bus.read(0x2008, 4), index == 6 ? 0x00f100f1U : 0x11118001U);
        CHECK_EQ(fixture.bus.read(0x200c, 4), index == 6 ? 0x00f100f1U : ids[index]);
        fixture.dma(0x08000000, 16, 1);
        CHECK_EQ(fixture.bus.read(0x2008, 4), 0x11118001U);
        CHECK_EQ(fixture.bus.read(0x200c, 4), ids[index]);
    }
}

TEST(flash_chips_apply_address_units_and_wrap_within_each_burst_window) {
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        auto& bus = fixture.bus;
        write_be32(bus.flashram.data() + 0x40, 0x12345678);
        write_be32(bus.flashram.data() + 0x80, 0xabcdef01);
        CHECK_EQ(bus.read(0x08000040, 4), index < 3 ? 0xabcdef01U : 0x12345678U);
        write_be32(bus.flashram.data() + 0x10000, 0x23456789);
        write_be32(bus.flashram.data() + 0x17ffc, 0x3456789a);
        write_be32(bus.flashram.data() + 0x18000, 0x456789ab);
        fixture.dma(index < 3 ? 0x0800bffeU : 0x08017ffcU, 8);
        CHECK_EQ(bus.read(0x2000, 4), 0x3456789aU);
        CHECK_EQ(bus.read(0x2004, 4), 0x23456789U);
    }
}

TEST(flash_chips_page_load_rules_and_programmed_data_follow_the_selected_chip) {
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        auto& bus = fixture.bus;
        fixture.command(0xb4000000);
        bus.write(0x08000000, 4, 0xf0ff55aa);
        bus.tick(140);
        bus.write(0x08000080, 4, 0x0ff0a55a);
        bus.tick(140);
        const u32 buffered = index == 6 ? 0x00f0050aU : 0x0ff0a55aU;
        CHECK_EQ(bus.read(0x08000000, 4), buffered);
        write_be32(bus.flashram.data() + 0x180, 0xffff0fff);
        fixture.command(0xa5000003);
        const u64 duration = index == 6 ? 18750U : 218750U;
        CHECK_EQ(fixture.status(), index == 6 ? 0x00010001U : 0x000d000dU);
        bus.tick(duration - 141);
        CHECK_EQ(fixture.status(), index == 6 ? 0x00010001U : 0x000d000dU);
        bus.tick(1);
        CHECK_EQ(fixture.status(), index == 6 ? 0x00840084U : 0x008c008cU);
        CHECK_EQ(bus.read(0x04300008, 4) & 16U, 0U);
        fixture.command(0xf0000000);
        CHECK_EQ(bus.read(index < 3 ? 0x080000c0U : 0x08000180U, 4), buffered & 0xffff0fffU);
        fixture.command(0xb4000000);
        CHECK_EQ(bus.read(0x08000000, 4), 0xffffffffU);
    }
}

TEST(flash_chips_status_entry_and_stale_reads_are_model_specific) {
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        auto& bus = fixture.bus;
        write_be32(bus.flashram.data(), 0x12345678);
        fixture.command(0xd2000000);
        CHECK_EQ(bus.read(0x08000000, 4), index == 6 ? 0x00800080U : 0x12345678U);
        fixture.command(0xd2000000);
        CHECK_EQ(bus.read(0x08000000, 4), index == 6 ? 0x00800080U : 0x5678008cU);
        fixture.command(0xf0000000);
        fixture.command(0xd2000000);
        CHECK_EQ(bus.read(0x08000000, 4), index == 6 ? 0x00800080U : 0x12345678U);
    }
}

TEST(flash_chips_erase_deadlines_distinguish_sector_and_whole_chip_operations) {
    for (unsigned index = 0; index < chips.size(); ++index)
        for (bool whole_chip : {false, true}) {
            FlashFixture fixture(chips[index]);
            auto& bus = fixture.bus;
            std::fill(bus.flashram.begin(), bus.flashram.end(), u8{0x5a});
            fixture.command(whole_chip ? 0x3c000000U : 0x4b000180U);
            fixture.command(0x78000000);
            const u64 duration = index == 6 ? whole_chip ? 18750000U : 17500000U : 5312500U;
            CHECK_EQ(fixture.status(), index == 6 ? 0x00020002U : 0x000e000eU);
            bus.tick(duration - 141);
            CHECK_EQ(fixture.status(), index == 6 ? 0x00020002U : 0x000e000eU);
            bus.tick(1);
            CHECK_EQ(fixture.status(), index == 6 ? 0x00880088U : 0x008c008cU);
            for (u32 offset = 0; offset < bus.flashram.size(); ++offset)
                CHECK_EQ(bus.flashram[offset],
                         whole_chip || (offset >= 0xc000 && offset < 0x10000) ? 0xffU : 0x5aU);
            CHECK_EQ(bus.read(0x04300008, 4) & 16U, 0U);
        }
}

TEST(flash_chips_matsushita_status_writes_clear_success_bits_and_completions_restore_them) {
    FlashFixture fixture(FlashChip::Mn63F81Mpn);
    auto& bus = fixture.bus;
    fixture.command(0xa5000000);
    bus.tick(18750);
    CHECK_EQ(fixture.status(), 0x00840084U);
    fixture.command(0x4b000080);
    fixture.command(0x78000000);
    CHECK_EQ(fixture.status(), 0x00060006U);
    bus.write(0x08000040, 4, 0x12345678);
    bus.tick(140);
    CHECK_EQ(fixture.status(), 0x00020002U);
    bus.tick(17500000 - 280);
    CHECK_EQ(fixture.status(), 0x00880088U);
    fixture.command(0xa5000001);
    bus.tick(18750);
    CHECK_EQ(fixture.status(), 0x008c008cU);
    bus.write(0x0800007c, 4, 0xffffffff);
    bus.tick(140);
    CHECK_EQ(fixture.status(), 0x00800080U);
}

TEST(flash_chips_silicon_id_open_bus_is_specific_to_the_matsushita_part) {
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        fixture.command(0xe1000000);
        CHECK_EQ(fixture.bus.read(0x08021234, 4), index == 6 ? 0x12341234U : 0x11118001U);
        CHECK_EQ(fixture.bus.read(0x08001230, 4), index == 6 ? 0x12301230U : 0x11118001U);
        fixture.command(0xe1000000);
        CHECK_EQ(fixture.bus.read(0x08001230, 4), 0x11118001U);
    }
}

TEST(flash_chips_reset_preserves_selection_and_saved_bytes_but_cancels_pending_work) {
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        auto& bus = fixture.bus;
        fixture.command(0xb4000000);
        bus.write(0x08000000, 4, 0x12345678);
        bus.tick(140);
        fixture.command(0xa5000001);
        fixture.system.reset();
        CHECK_EQ(bus.flash_chip(), chips[index]);
        CHECK_EQ(bus.read(index < 3 ? 0x08000040U : 0x08000080U, 4), 0x12345678U);
        fixture.command(0xd2000000);
        fixture.command(0xd2000000);
        bus.tick(218750);
        CHECK_EQ(fixture.status(), index == 6 ? 0x00800080U : 0x008c008cU);
        fixture.command(0xb4000000);
        CHECK_EQ(bus.read(0x08000000, 4), 0xffffffffU);
    }
}

TEST(flash_chips_configuration_changes_clear_volatile_state_without_replacing_storage) {
    FlashFixture fixture(FlashChip::Mx29L1100);
    auto& bus = fixture.bus;
    fixture.command(0xa5000000);
    write_be32(bus.flashram.data() + 0x40, 0x12345678);
    bus.set_flash_chip(FlashChip::Mn63F81Mpn);
    CHECK_EQ(bus.read(0x08000040, 4), 0x12345678U);
    fixture.command(0xd2000000);
    bus.tick(218750);
    CHECK_EQ(fixture.status(), 0x00800080U);
    bus.set_flash_chip(FlashChip::Mx29L0000);
    CHECK_EQ(bus.read(0x08000020, 4), 0x12345678U);
}

TEST(flash_chips_busy_commands_do_not_change_mode_or_start_another_program) {
    for (unsigned index = 0; index < chips.size(); ++index) {
        FlashFixture fixture(chips[index]);
        auto& bus = fixture.bus;
        fixture.command(0xb4000000);
        bus.write(0x08000000, 4, 0x12345678);
        bus.tick(140);
        fixture.command(0xa5000003);
        fixture.command(0xf0000000);
        fixture.command(0xb4000000);
        fixture.command(0xe1000000);
        fixture.command(0xa5000004);
        CHECK_EQ(fixture.status(), index == 6 ? 0x00010001U : 0x000d000dU);
        CHECK_EQ(read_be32(bus.flashram.data() + 3 * 128), 0x12345678U);
        CHECK_EQ(read_be32(bus.flashram.data() + 4 * 128), 0xffffffffU);
        const u64 duration = index == 6 ? 18750U : 218750U;
        bus.tick(duration - 701);
        CHECK_EQ(fixture.status(), index == 6 ? 0x00010001U : 0x000d000dU);
        bus.tick(1);
        CHECK_EQ(fixture.status(), index == 6 ? 0x00840084U : 0x008c008cU);
    }
}

TEST(flash_chips_reset_cancels_erase_setup_and_completion_status) {
    for (unsigned index = 0; index < chips.size(); ++index)
        for (bool start_erase : {false, true}) {
            FlashFixture fixture(chips[index]);
            auto& bus = fixture.bus;
            std::fill(bus.flashram.begin(), bus.flashram.end(), u8{0x12});
            fixture.command(0x4b000180);
            if (start_erase)
                fixture.command(0x78000000);
            fixture.system.reset();
            fixture.command(0x78000000);
            for (u32 offset = 0; offset < bus.flashram.size(); ++offset)
                CHECK_EQ(bus.flashram[offset],
                         start_erase && offset >= 0xc000 && offset < 0x10000 ? 0xffU : 0x12U);
            fixture.command(0xd2000000);
            fixture.command(0xd2000000);
            bus.tick(17500000);
            CHECK_EQ(fixture.status(), index == 6 ? 0x00800080U : 0x008c008cU);
        }
}
