#include "host_fixture.hpp"

#include "cupid/host/hardware.hpp"
#include "cupid/host/storage.hpp"

#include <algorithm>
#include <array>
#include <filesystem>

using namespace cupid;
using namespace cupid::host;

namespace {

std::span<u8> save_bytes(System& system) {
    switch (system.bus.save_type) {
    case SaveType::Sram:
        return system.bus.sram;
    case SaveType::FlashRam:
        return system.bus.flashram;
    case SaveType::Eeprom4K:
    case SaveType::Eeprom16K:
        return system.bus.eeprom;
    case SaveType::None:
        return {};
    }
    return {};
}

void check_restart(test::host::TempDirectory& directory, host::Options options, std::size_t expected_size,
                   u8 fresh_value) {
    const auto file = directory.path() / "cart-save.bin";
    options.save_file = file;
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    auto bytes = save_bytes(*system);
    CHECK_EQ(bytes.size(), expected_size);
    CHECK(std::all_of(bytes.begin(), bytes.end(), [fresh_value](u8 value) { return value == fresh_value; }));
    bytes.front() = 0x12;
    bytes.back() = 0x34;
    CHECK(flush_persistent_storage(*system, options, error));
    system.reset();
    CHECK_EQ(std::filesystem::file_size(file), expected_size);
    system = create_system(options, error);
    CHECK(system != nullptr);
    bytes = save_bytes(*system);
    CHECK_EQ(bytes.front(), 0x12U);
    CHECK_EQ(bytes.back(), 0x34U);
}

} // namespace

TEST(host_storage_roundtrips_every_sram_and_eeprom_size) {
    for (const unsigned size : {32U * 1024U, 96U * 1024U, 128U * 1024U}) {
        test::host::TempDirectory directory;
        auto options = test::host::base_options(directory);
        options.save = SaveType::Sram;
        options.sram_bytes = size;
        check_restart(directory, options, size, 0);
    }
    for (const auto& [type, size] : std::array{std::pair{SaveType::Eeprom4K, std::size_t{512}},
                                               std::pair{SaveType::Eeprom16K, std::size_t{2048}}}) {
        test::host::TempDirectory directory;
        auto options = test::host::base_options(directory);
        options.save = type;
        check_restart(directory, options, size, 0xff);
    }
}

TEST(host_storage_roundtrips_flash_for_every_supported_part) {
    constexpr std::array chips{FlashChip::Mx29L0000,  FlashChip::Mx29L0001,  FlashChip::Mx29L1100,
                               FlashChip::Mx29L1101A, FlashChip::Mx29L1101B, FlashChip::Mx29L1101C,
                               FlashChip::Mn63F81Mpn};
    for (const auto chip : chips) {
        test::host::TempDirectory directory;
        auto options = test::host::base_options(directory);
        options.save = SaveType::FlashRam;
        options.flash_chip = chip;
        check_restart(directory, options, 128U * 1024U, 0xff);
    }
}

TEST(host_storage_rejects_wrong_cartridge_save_sizes_without_partial_load) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.save = SaveType::Sram;
    options.sram_bytes = 32U * 1024U;
    options.save_file = directory.write("bad-save.bin", std::vector<u8>(32U * 1024U - 1U, 0x77));
    std::string error;
    CHECK(!create_system(options, error));
    CHECK(error.find("exactly 32768 bytes") != std::string::npos);
}

TEST(host_storage_rejects_wrong_pak_transfer_and_rtc_sizes) {
    test::host::TempDirectory directory;
    std::string error;

    auto options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::ControllerPak;
    options.ports[0].pak_file = directory.write("short.pak", std::vector<u8>(32U * 1024U - 1U));
    CHECK(!create_system(options, error));
    CHECK(error.find("exactly 32768 bytes") != std::string::npos);

    options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
    options.ports[0].transfer.cartridge = directory.write("ram.gb", test::host::game_boy_rom());
    options.ports[0].transfer.save_file = directory.write("short.sav", std::vector<u8>(0x7fff));
    CHECK(!create_system(options, error));
    CHECK(error.find("exactly 32768 bytes") != std::string::npos);

    options = test::host::base_options(directory);
    options.rtc = true;
    options.rtc_file = directory.write("short.rtc", std::vector<u8>(31));
    CHECK(!create_system(options, error));
    CHECK(error.find("exactly 32 bytes") != std::string::npos);

    options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
    options.ports[0].transfer.cartridge = directory.write("rtc.gb", test::host::game_boy_rom(0x10));
    options.ports[0].transfer.rtc_file = directory.write("short.gbrt", std::vector<u8>(11));
    CHECK(!create_system(options, error));
    CHECK(error.find("exactly 12 bytes") != std::string::npos);
}

TEST(host_storage_roundtrips_controller_pak_across_system_restart) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.ports[1].controller.connected = true;
    options.ports[1].controller.accessory = ControllerAccessory::ControllerPak;
    options.ports[1].pak_file = directory.path() / "controller.pak";
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK_EQ(system->bus.controller_paks[1][0], 0U);
    system->bus.controller_paks[1][0] = 0x45;
    system->bus.controller_paks[1].back() = 0x67;
    CHECK(flush_persistent_storage(*system, options, error));
    system.reset();
    system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK_EQ(system->bus.controller_paks[1][0], 0x45U);
    CHECK_EQ(system->bus.controller_paks[1].back(), 0x67U);
}

TEST(host_storage_roundtrips_transfer_pak_ram_for_mbc1_and_mbc2) {
    for (const auto& [type, size] :
         std::array{std::pair{u8{0x03}, std::size_t{0x8000}}, std::pair{u8{0x06}, std::size_t{256}}}) {
        test::host::TempDirectory directory;
        auto options = test::host::base_options(directory);
        options.ports[0].controller.connected = true;
        options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
        options.ports[0].transfer.cartridge = directory.write("game.gb", test::host::game_boy_rom(type));
        options.ports[0].transfer.save_file = directory.path() / "game.sav";
        std::string error;
        auto system = create_system(options, error);
        CHECK(system != nullptr);
        auto* cartridge = system->bus.transfer_paks[0].cartridge();
        CHECK(cartridge != nullptr);
        CHECK_EQ(cartridge->ram().size(), size);
        cartridge->ram().front() = 0x89;
        cartridge->ram().back() = 0xab;
        CHECK(flush_persistent_storage(*system, options, error));
        system.reset();
        system = create_system(options, error);
        CHECK(system != nullptr);
        cartridge = system->bus.transfer_paks[0].cartridge();
        CHECK(cartridge != nullptr);
        CHECK_EQ(cartridge->ram().front(), 0x89U);
        CHECK_EQ(cartridge->ram().back(), 0xabU);
    }
}

TEST(host_storage_roundtrips_cartridge_and_game_boy_rtc_state) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.rtc = true;
    options.rtc_file = directory.path() / "cart.rtc";
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
    options.ports[0].transfer.cartridge = directory.write("clock.gb", test::host::game_boy_rom(0x10));
    options.ports[0].transfer.rtc_file = directory.path() / "clock.gbrt";
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    CartridgeRtc::Registers registers{};
    for (unsigned index = 0; index < registers.size(); ++index)
        registers[index] = static_cast<u8>(index * 3U);
    system->bus.rtc.emplace(registers);
    auto* game_boy = system->bus.transfer_paks[0].cartridge();
    CHECK(game_boy != nullptr);
    game_boy->set_clock({59, 58, 23, 511, true, true});
    CHECK(flush_persistent_storage(*system, options, error));
    system.reset();

    CHECK_EQ(std::filesystem::file_size(options.rtc_file), std::size_t{32});
    CHECK_EQ(std::filesystem::file_size(options.ports[0].transfer.rtc_file), std::size_t{12});
    system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK_EQ(system->bus.rtc->registers(), registers);
    game_boy = system->bus.transfer_paks[0].cartridge();
    CHECK(game_boy != nullptr);
    const auto clock = game_boy->clock();
    CHECK_EQ(clock.seconds, 59U);
    CHECK_EQ(clock.minutes, 58U);
    CHECK_EQ(clock.hours, 23U);
    CHECK_EQ(clock.days, 511U);
    CHECK(clock.halted);
    CHECK(clock.carry);
}

TEST(host_storage_rejects_malformed_game_boy_rtc_records) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
    options.ports[0].transfer.cartridge = directory.write("clock.gb", test::host::game_boy_rom(0x10));
    std::vector<u8> record(12);
    record[0] = 'B';
    options.ports[0].transfer.rtc_file = directory.write("bad.gbrt", record);
    std::string error;
    CHECK(!create_system(options, error));
    CHECK(error.find("GBRT v1") != std::string::npos);
}

TEST(host_storage_rejects_rom_pif_and_storage_path_collisions) {
    test::host::TempDirectory directory;
    std::string error;
    auto options = test::host::base_options(directory);
    options.save = SaveType::Sram;
    options.save_file = options.cartridge;
    CHECK(!create_system(options, error));
    CHECK(error.find("cartridge ROM") != std::string::npos);

    options = test::host::base_options(directory);
    options.rtc = true;
    options.rtc_file = directory.path() / "shared.bin";
    options.save = SaveType::Sram;
    options.save_file = options.rtc_file;
    CHECK(!create_system(options, error));
    CHECK(error.find("another storage file") != std::string::npos);

    options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::ControllerPak;
    options.ports[0].pak_file = options.pif;
    CHECK(!create_system(options, error));
    CHECK(error.find("PIF ROM") != std::string::npos);

    options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::TransferPak;
    options.ports[0].transfer.cartridge = directory.write("game.gb", test::host::game_boy_rom());
    options.ports[0].transfer.save_file = options.ports[0].transfer.cartridge;
    CHECK(!create_system(options, error));
    CHECK(error.find("Transfer Pak ROM") != std::string::npos);
}

TEST(host_storage_atomic_failure_keeps_the_previous_valid_image) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.save = SaveType::Sram;
    options.sram_bytes = 32U * 1024U;
    const std::vector<u8> original(options.sram_bytes, 0x5a);
    options.save_file = directory.write("protected.sra", original);
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    system->bus.sram[0] = 0xa5;
    for (unsigned index = 0; index < 100; ++index) {
        auto occupied = options.save_file;
        occupied += ".tmp." + std::to_string(index);
        CHECK(std::filesystem::create_directory(occupied));
    }
    CHECK(!flush_persistent_storage(*system, options, error));
    CHECK(error.find("temporary file") != std::string::npos);
    CHECK_EQ(directory.read(options.save_file), original);
}

TEST(host_storage_dangling_temporary_symlink_is_never_followed_or_replaced) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.save = SaveType::Sram;
    options.sram_bytes = 32U * 1024U;
    const std::vector<u8> original(options.sram_bytes, 0x5a);
    options.save_file = directory.write("protected.sra", original);

    auto dangling = options.save_file;
    dangling += ".tmp.0";
    const auto victim = directory.path() / "must-not-be-created.bin";
    auto occupied = options.save_file;
    occupied += ".tmp.1";
    const std::vector<u8> occupied_contents{0x44, 0x55, 0x66};
    directory.write("protected.sra.tmp.1", occupied_contents);
    std::error_code code;
    std::filesystem::create_symlink(victim, dangling, code);
    CHECK(!code);
    CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(dangling)));
    CHECK(!std::filesystem::exists(victim));

    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    system->bus.sram.assign(options.sram_bytes, 0xa5);
    CHECK(flush_persistent_storage(*system, options, error));

    CHECK(!std::filesystem::exists(victim));
    CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(dangling)));
    CHECK_EQ(directory.read(occupied), occupied_contents);
    CHECK_EQ(directory.read(options.save_file), std::vector<u8>(options.sram_bytes, 0xa5));
}

TEST(host_storage_reports_unavailable_output_directories) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.save = SaveType::Sram;
    options.save_file = directory.path() / "missing" / "save.sra";
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK(!flush_persistent_storage(*system, options, error));
    CHECK(error.find("directory is unavailable") != std::string::npos);
}
