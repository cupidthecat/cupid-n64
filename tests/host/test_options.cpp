#include "host_fixture.hpp"

#include "cupid/host/options.hpp"

#include <array>

using namespace cupid;
using namespace cupid::host;

TEST(host_options_require_explicit_cartridge_save_hardware) {
    std::string error;
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom"}, error));
    CHECK(error.find("--save") != std::string::npos);

    const auto options = test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none"}, error);
    CHECK(options.has_value());
    CHECK_EQ(options->save, SaveType::None);
}

TEST(host_options_cover_every_cartridge_save_capacity_and_flash_part) {
    struct SaveChoice {
        std::string_view name;
        SaveType type;
        unsigned bytes;
    };
    constexpr std::array saves{
        SaveChoice{"none", SaveType::None, 0},
        SaveChoice{"sram32", SaveType::Sram, 32U * 1024U},
        SaveChoice{"sram96", SaveType::Sram, 96U * 1024U},
        SaveChoice{"sram128", SaveType::Sram, 128U * 1024U},
        SaveChoice{"eeprom4k", SaveType::Eeprom4K, 512},
        SaveChoice{"eeprom16k", SaveType::Eeprom16K, 2048},
    };
    std::string error;
    for (const auto& choice : saves) {
        const auto options =
            test::host::parse({"game.z64", "--pif", "pif.rom", "--save", choice.name}, error);
        CHECK(options.has_value());
        CHECK_EQ(options->save, choice.type);
        if (choice.type == SaveType::Sram)
            CHECK_EQ(options->sram_bytes, choice.bytes);
    }

    constexpr std::array chips{"mx29l0000",  "mx29l0001",  "mx29l1100", "mx29l1101a",
                               "mx29l1101b", "mx29l1101c", "mn63f81mpn"};
    for (const auto chip : chips) {
        const auto options = test::host::parse(
            {"game.z64", "--pif", "pif.rom", "--save", "flash", "--flash-chip", chip}, error);
        CHECK(options.has_value());
        CHECK_EQ(options->save, SaveType::FlashRam);
        CHECK(options->flash_chip.has_value());
    }
}

TEST(host_options_reject_incompatible_flash_and_storage_combinations) {
    std::string error;
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "flash"}, error));
    CHECK(error.find("--flash-chip") != std::string::npos);
    CHECK(!test::host::parse(
        {"game.z64", "--pif", "pif.rom", "--save", "sram32", "--flash-chip", "mx29l1100"}, error));
    CHECK(error.find("--save flash") != std::string::npos);
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--save-file", "save.bin"},
                             error));
    CHECK(error.find("--save-file") != std::string::npos);
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--rtc-file", "rtc.bin"},
                             error));
    CHECK(error.find("--rtc-file requires --rtc") != std::string::npos);
}

TEST(host_options_make_controller_accessories_and_transfer_hardware_explicit) {
    std::string error;
    auto options = test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none"}, error);
    CHECK(options.has_value());
    for (const auto& port : options->ports) {
        CHECK(!port.controller.connected);
        CHECK_EQ(port.controller.accessory, ControllerAccessory::None);
    }

    options = test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller",
                                 "2:gamepad", "--accessory", "2:controller-pak", "--pak-file", "2:pak.bin"},
                                error);
    CHECK(options.has_value());
    CHECK(options->ports[1].controller.connected);
    CHECK_EQ(options->ports[1].controller.accessory, ControllerAccessory::ControllerPak);
    CHECK(!options->ports[1].pak_file.empty());

    CHECK(!test::host::parse(
        {"game.z64", "--pif", "pif.rom", "--save", "none", "--accessory", "1:controller-pak"}, error));
    CHECK(error.find("connected gamepad") != std::string::npos);
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller", "1:gamepad",
                              "--accessory", "1:transfer-pak", "--transfer-ram", "1:32768"},
                             error));
    CHECK(error.find("--transfer-rom") != std::string::npos);
}

TEST(host_options_expose_region_ram_cic_rtc_and_extended_validation) {
    std::string error;
    const auto options = test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--region",
                                            "pal", "--pif-region", "pal", "--ram", "4", "--cic", "6105",
                                            "--rtc", "--require-extended-tests"},
                                           error);
    CHECK(options.has_value());
    CHECK_EQ(options->region, VideoStandard::Pal);
    CHECK_EQ(options->pif_region, VideoStandard::Pal);
    CHECK_EQ(options->ram_mib, 4U);
    CHECK_EQ(options->cic, CicModel::Nus6105);
    CHECK(options->rtc);
    CHECK(options->require_extended);
    CHECK(options->require_success);
}

TEST(host_option_paths_preserve_utf8_text) {
    const std::u8string encoded = u8"save-測試-é.bin";
    const std::string text(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    const auto path = argument_path(text);
    CHECK_EQ(path_text(path), text);
}
