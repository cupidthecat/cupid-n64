#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

using namespace cupid;

u8 pak_address_crc(u16 address) {
    u8 crc = 0;
    for (unsigned index = 0; index < 16; ++index) {
        const u8 feedback = (crc & 0x10U) != 0 ? 0x15U : 0U;
        crc = static_cast<u8>((static_cast<unsigned>(crc) << 1U) | ((address & 0x8000U) != 0 ? 1U : 0U));
        address = static_cast<u16>(address << 1U);
        crc ^= feedback;
    }
    return static_cast<u8>(crc & 0x1fU);
}

void fill_cart_pattern(Bus& bus, std::size_t size = 512) {
    bus.rom.resize(size);
    for (std::size_t index = 0; index < size; ++index)
        bus.rom[index] = static_cast<u8>(index);
}

void run_si_read(Bus& bus, u32 dram = 0x2000) {
    bus.write(0x04800000U, 4, dram);
    bus.write(0x04800004U, 4, 0x1fc007c0U);
    CHECK((bus.read(0x04800018U, 4) & 1U) != 0);
    for (unsigned step = 0; step < 1500 && (bus.read(0x04800018U, 4) & 1U) != 0; ++step)
        bus.tick(100);
    CHECK((bus.read(0x04800018U, 4) & 1U) == 0);
    CHECK((bus.read(0x04800018U, 4) & (1U << 12U)) != 0);
    bus.write(0x04800018U, 4, 0);
}

} // namespace

TEST(bus_cart_subword_reads_follow_pi_halfword_bus_lanes) {
    System system;
    auto& bus = system.bus;
    bus.rom = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
               0x21, 0x43, 0x65, 0x87, 0x99, 0xba, 0xdc, 0xfe};

    CHECK_EQ(bus.read(0x10000000U, 4), 0x01234567ULL);
    CHECK_EQ(bus.read(0x10000000U, 2), 0x0123ULL);
    CHECK_EQ(bus.read(0x10000002U, 2), 0x89abULL);
    CHECK_EQ(bus.read(0x10000002U, 1), 0x89ULL);
    CHECK_EQ(bus.read(0x10000003U, 1), 0xabULL);
    CHECK_EQ(bus.read(0x10000004U, 2), 0x89abULL);
}

TEST(bus_cart_write_latch_expires_and_isviewer_forces_completion) {
    System system;
    auto& bus = system.bus;
    bus.rom = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    bus.write(0x10000000U, 4, 0xbadc0ffeU);
    CHECK((bus.read(0x04600010U, 4) & 2U) != 0);
    bus.tick(20);
    CHECK((bus.read(0x04600010U, 4) & 2U) != 0);
    bus.tick(120);
    CHECK((bus.read(0x04600010U, 4) & 2U) == 0);
    CHECK_EQ(bus.read(0x10000000U, 4), 0x01234567ULL);

    bus.write(0x13ff0014U, 4, 0);
    CHECK((bus.read(0x04600010U, 4) & 2U) == 0);
    bus.write(0x04600004U, 4, 0xfedcba97U);
    CHECK_EQ(bus.read(0x04600004U, 4), 0xfedcba96ULL);
}

TEST(bus_pi_dma_preserves_odd_transfer_tail_and_address_progress) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    fill_cart_pattern(bus);

    constexpr u32 dram = 0x1000;
    bus.write(0x04600000U, 4, dram);
    bus.write(0x04600004U, 4, 0x10000000U);
    bus.write(0x0460000cU, 4, 126U);

    for (u32 index = 0; index < 128; ++index) {
        CHECK_EQ(bus.read_ram_byte(dram + index), static_cast<u8>(index));
    }
    CHECK_EQ(bus.read(0x04600004U, 4), 0x10000080ULL);
    CHECK_EQ(bus.read(0x04600000U, 4), 0x00001080ULL);
    CHECK((bus.read(0x04600010U, 4) & 1U) != 0);

    bus.tick(1000);
    CHECK((bus.read(0x04600010U, 4) & 1U) == 0);
    CHECK((bus.read(0x04600010U, 4) & 8U) != 0);
    CHECK((bus.read(0x04300008U, 4) & (1U << 4U)) != 0);
}

TEST(bus_pif_rom_is_immutable_but_ram_keeps_rcp_store_lane_behavior) {
    System system;
    auto& bus = system.bus;
    bus.pif[0] = 0x12;
    bus.pif[1] = 0x34;
    bus.pif[2] = 0x56;
    bus.pif[3] = 0x78;
    bus.write(0x1fc00000U, 4, 0xdeadbeefU);
    bus.tick(2150);
    CHECK_EQ(bus.read(0x1fc00000U, 4), 0x12345678ULL);

    bus.write(0x1fc007c0U, 4, 0xdeadbeefU);
    bus.tick(2150);
    bus.write(0x1fc007c1U, 1, 0x12345678U);
    bus.tick(2150);
    CHECK_EQ(bus.read(0x1fc007c0U, 4), 0x56780000ULL);
}

TEST(bus_controller_pak_rejects_bad_address_crc_without_writing) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.controller_paks[0][0] = 0xa5;

    auto prepare_write = [&](u8 address_crc) {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        bus.pif[0x7c0] = 35;
        bus.pif[0x7c1] = 1;
        bus.pif[0x7c2] = 0x03;
        bus.pif[0x7c3] = 0x00;
        bus.pif[0x7c4] = address_crc;
        for (unsigned index = 0; index < 32; ++index)
            bus.pif[0x7c5 + index] = static_cast<u8>(index + 1);
        bus.pif[0x7c0 + 38] = 0xfe;
    };

    prepare_write(static_cast<u8>(pak_address_crc(0) ^ 1U));
    run_si_read(bus);
    CHECK_EQ(bus.controller_paks[0][0], 0xa5U);

    prepare_write(pak_address_crc(0));
    run_si_read(bus);
    for (unsigned index = 0; index < 32; ++index) {
        CHECK_EQ(bus.controller_paks[0][index], static_cast<u8>(index + 1));
    }
}

TEST(bus_pif_control_uses_cic_seed_and_hides_boot_secrets) {
    System system;
    auto& bus = system.bus;
    bus.cic.configure(CicModel::Nus6105);
    bus.reset();
    CHECK_EQ(bus.pif[0x7e5], 0x04U);
    CHECK_EQ(bus.pif[0x7e6], 0x91U);
    CHECK_EQ(bus.pif[0x7e7], 0x91U);

    const std::array<u8, 6> checksum{0x86, 0x18, 0xa4, 0x5b, 0xc2, 0xd3};
    std::copy(checksum.begin(), checksum.end(), bus.pif.begin() + 0x7f2);
    bus.write(0x1fc007fcU, 4, 0x00000020U);
    CHECK((bus.pif[0x7ff] & 0x80U) != 0);
    CHECK_EQ(bus.pif[0x7e5], 0U);
    CHECK_EQ(bus.pif[0x7e6], 0U);
    CHECK_EQ(bus.pif[0x7e7], 0U);
    for (unsigned index = 0; index < checksum.size(); ++index)
        CHECK_EQ(bus.pif[0x7f2 + index], 0U);

    bus.tick(2150);
    bus.write(0x1fc007fcU, 4, 0x00000008U);
    CHECK_EQ(bus.pif[0x7ff], 0U);
}

TEST(bus_load_rom_normalizes_byte_order_before_cart_reads) {
    System system;
    std::string error;
    std::vector<u8> swapped{0x37, 0x80, 0x40, 0x12, 0x23, 0x01, 0x67, 0x45};
    CHECK(system.bus.load_rom(std::move(swapped), error));
    CHECK(error.empty());
    CHECK_EQ(system.bus.rom[0], 0x80U);
    CHECK_EQ(system.bus.rom[1], 0x37U);
    CHECK_EQ(system.bus.rom[2], 0x12U);
    CHECK_EQ(system.bus.rom[3], 0x40U);
    CHECK_EQ(system.bus.read(0x10000004U, 4), 0x01234567ULL);
}

TEST(bus_sram_roundtrips_through_pi_dma) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Sram);

    constexpr u32 source = 0x3000;
    constexpr u32 target = 0x4000;
    const std::array<u8, 8> expected{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    for (unsigned index = 0; index < expected.size(); ++index) {
        bus.write_ram_byte(source + index, expected[index]);
        bus.write_ram_byte(target + index, 0);
    }

    bus.write(0x04600000U, 4, source);
    bus.write(0x04600004U, 4, 0x08000000U);
    bus.write(0x04600008U, 4, 7U);
    bus.tick(1000);
    CHECK((bus.read(0x04600010U, 4) & 8U) != 0);
    bus.write(0x04600010U, 4, 2U);

    bus.write(0x04600000U, 4, target);
    bus.write(0x04600004U, 4, 0x08000000U);
    bus.write(0x0460000cU, 4, 7U);
    bus.tick(1000);
    for (unsigned index = 0; index < expected.size(); ++index) {
        CHECK_EQ(bus.read_ram_byte(target + index), expected[index]);
    }
}

TEST(bus_eeprom_joybus_write_busy_and_readback) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Eeprom4K);

    const std::array<u8, 8> payload{0x03, 0x14, 0x25, 0x36, 0x47, 0x58, 0x69, 0x7a};
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c4] = 10;
    bus.pif[0x7c5] = 1;
    bus.pif[0x7c6] = 0x05;
    bus.pif[0x7c7] = 3;
    std::copy(payload.begin(), payload.end(), bus.pif.begin() + 0x7c8);
    run_si_read(bus, 0x5000);
    CHECK_EQ(bus.read_ram_byte(0x5010), 0U);

    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c4] = 2;
    bus.pif[0x7c5] = 8;
    bus.pif[0x7c6] = 0x04;
    bus.pif[0x7c7] = 3;
    run_si_read(bus, 0x5100);
    for (unsigned index = 0; index < payload.size(); ++index)
        CHECK_EQ(bus.read_ram_byte(0x5108 + index), 0xffU);

    bus.tick(375000);
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c4] = 2;
    bus.pif[0x7c5] = 8;
    bus.pif[0x7c6] = 0x04;
    bus.pif[0x7c7] = 3;
    run_si_read(bus, 0x5200);
    for (unsigned index = 0; index < payload.size(); ++index)
        CHECK_EQ(bus.read_ram_byte(0x5208 + index), payload[index]);
}

TEST(bus_flashram_load_and_program_page_uses_pi_data_path) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::FlashRam);

    constexpr u32 source = 0x6000;
    for (u32 index = 0; index < 128; ++index)
        bus.write_ram_byte(source + index, static_cast<u8>(index ^ 0xa5U));

    bus.write(0x08010000U, 4, 0xb4000000U);
    bus.tick(200);
    bus.write(0x04600000U, 4, source);
    bus.write(0x04600004U, 4, 0x08000000U);
    bus.write(0x04600008U, 4, 127U);
    bus.tick(1000);
    bus.write(0x04600010U, 4, 2U);

    bus.write(0x08010000U, 4, 0xa5000001U);
    bus.tick(218750);
    bus.write(0x08010000U, 4, 0xf0000000U);
    bus.tick(200);

    bus.write(0x0460002cU, 4, 6);
    bus.write(0x04600000U, 4, 0x7000);
    bus.write(0x04600004U, 4, 0x08000040);
    bus.write(0x0460000cU, 4, 127);
    bus.tick(1000);
    for (u32 index = 0; index < 128; ++index)
        CHECK_EQ(bus.read_ram_byte(0x7000 + index), static_cast<u8>(index ^ 0xa5U));
}
