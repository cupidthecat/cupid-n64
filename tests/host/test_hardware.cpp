#include "host_fixture.hpp"

#include "cupid/host/hardware.hpp"

#include <array>

using namespace cupid;
using namespace cupid::host;

TEST(host_hardware_requires_explicit_save_choice_even_for_product_codes) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    auto rom = test::host::n64_rom();
    rom[0x3b] = 'N';
    rom[0x3c] = 'S';
    rom[0x3d] = 'M';
    options.cartridge = directory.write("nsm.z64", rom);
    options.save.reset();
    std::string error;
    CHECK(!create_system(options, error));
    CHECK(error.find("ambiguous") != std::string::npos);
}

TEST(host_hardware_configures_all_sram_and_eeprom_capacities) {
    test::host::TempDirectory directory;
    std::string error;
    for (const unsigned size : {32U * 1024U, 96U * 1024U, 128U * 1024U}) {
        auto options = test::host::base_options(directory);
        options.save = SaveType::Sram;
        options.sram_bytes = size;
        auto system = create_system(options, error);
        CHECK(system != nullptr);
        CHECK_EQ(system->bus.save_type, SaveType::Sram);
        CHECK_EQ(system->bus.sram.size(), size);
    }
    for (const auto& [type, size] : std::array{std::pair{SaveType::Eeprom4K, std::size_t{512}},
                                               std::pair{SaveType::Eeprom16K, std::size_t{2048}}}) {
        auto options = test::host::base_options(directory);
        options.save = type;
        auto system = create_system(options, error);
        CHECK(system != nullptr);
        CHECK_EQ(system->bus.eeprom.size(), size);
    }
}

TEST(host_hardware_configures_every_flash_part) {
    constexpr std::array chips{FlashChip::Mx29L0000,  FlashChip::Mx29L0001,  FlashChip::Mx29L1100,
                               FlashChip::Mx29L1101A, FlashChip::Mx29L1101B, FlashChip::Mx29L1101C,
                               FlashChip::Mn63F81Mpn};
    test::host::TempDirectory directory;
    std::string error;
    for (const auto chip : chips) {
        auto options = test::host::base_options(directory);
        options.save = SaveType::FlashRam;
        options.flash_chip = chip;
        auto system = create_system(options, error);
        CHECK(system != nullptr);
        CHECK_EQ(system->bus.flash_chip(), chip);
        CHECK_EQ(system->bus.flashram.size(), std::size_t{128 * 1024});
    }
}

TEST(host_hardware_applies_ram_region_and_cic_overrides_to_boot_state) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.ram_mib = 4;
    options.cic = CicModel::Nus6105;
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK_EQ(system->bus.rdram.size(), std::size_t{4 * 1024 * 1024});
    CHECK_EQ(system->video_standard(), VideoStandard::Ntsc);
    CHECK_EQ(system->bus.cic.model(), CicModel::Nus6105);
    CHECK_EQ(system->bus.pif[0x7e6], 0x91U);
    CHECK_EQ(system->bus.pif[0x7e7], 0x91U);
}

TEST(host_hardware_rejects_region_firmware_and_cic_mismatches) {
    test::host::TempDirectory directory;
    std::string error;
    auto options = test::host::base_options(directory);
    options.region = VideoStandard::Ntsc;
    options.pif_region = VideoStandard::Pal;
    CHECK(!create_system(options, error));
    CHECK(error.find("PIF firmware") != std::string::npos);

    options = test::host::base_options(directory);
    options.cic = CicModel::Nus7102;
    CHECK(!create_system(options, error));
    CHECK(error.find("different regions") != std::string::npos);
}

TEST(host_hardware_rejects_unknown_header_region_without_override) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.cartridge = directory.write("unknown-region.z64", test::host::n64_rom('?'));
    options.region.reset();
    std::string error;
    CHECK(!create_system(options, error));
    CHECK(error.find("header region") != std::string::npos);

    options.region = VideoStandard::Ntsc;
    CHECK(create_system(options, error) != nullptr);
}

TEST(host_hardware_only_attaches_selected_controllers_and_accessories) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.ports[2].controller.connected = true;
    options.ports[2].controller.accessory = ControllerAccessory::ControllerPak;
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    for (unsigned port = 0; port < 4; ++port) {
        CHECK_EQ(system->bus.controllers()[port].connected, port == 2);
        CHECK_EQ(system->bus.controllers()[port].accessory,
                 port == 2 ? ControllerAccessory::ControllerPak : ControllerAccessory::None);
    }
}

TEST(host_hardware_configures_transfer_pak_ram_clock_and_rumble_overrides) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
    options.ports[0].transfer.cartridge = directory.write("game.gb", test::host::game_boy_rom(0x1b, 0, 3));
    options.ports[0].transfer.mapper = GameBoyMapper::Mbc5;
    options.ports[0].transfer.ram_bytes = 0x8000;
    options.ports[0].transfer.clock = false;
    options.ports[0].transfer.rumble = false;
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    const auto* cartridge = system->bus.transfer_paks[0].cartridge();
    CHECK(cartridge != nullptr);
    CHECK_EQ(cartridge->config().mapper, GameBoyMapper::Mbc5);
    CHECK_EQ(cartridge->ram().size(), std::size_t{0x8000});
    CHECK(!cartridge->config().clock);
    CHECK(!cartridge->config().rumble);
}

TEST(host_hardware_attaches_cartridge_rtc_only_when_requested) {
    test::host::TempDirectory directory;
    std::string error;
    auto options = test::host::base_options(directory);
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK(!system->bus.rtc.has_value());
    options.rtc = true;
    system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK(system->bus.rtc.has_value());
}
