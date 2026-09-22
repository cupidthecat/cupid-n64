#include "host_fixture.hpp"

#include "cupid/host/media.hpp"
#include "cupid/host/preferences.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace cupid;
using namespace cupid::host;

namespace {

std::vector<u8> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t encoded_payload_size(const std::vector<u8>& bytes) {
    const auto newline = std::find(bytes.begin(), bytes.end(), static_cast<u8>('\n'));
    CHECK(newline != bytes.end());
    return static_cast<std::size_t>(bytes.end() - newline - 1);
}

void check_paths_equal(const std::filesystem::path& left, const std::filesystem::path& right) {
    CHECK_EQ(path_text(left), path_text(right));
}

void check_options_equal(const Options& left, const Options& right, bool persisted = false) {
    check_paths_equal(left.cartridge, right.cartridge);
    check_paths_equal(left.pif, right.pif);
    check_paths_equal(left.save_file, right.save_file);
    check_paths_equal(left.rtc_file, right.rtc_file);
    CHECK_EQ(left.region, right.region);
    CHECK_EQ(left.pif_region, right.pif_region);
    CHECK_EQ(left.ram_mib, right.ram_mib);
    CHECK_EQ(left.save, right.save);
    CHECK_EQ(left.sram_bytes, right.sram_bytes);
    CHECK_EQ(left.flash_chip, right.flash_chip);
    CHECK_EQ(left.cic, right.cic);
    CHECK_EQ(left.rtc, right.rtc);
    for (unsigned index = 0; index < left.ports.size(); ++index) {
        const auto& a = left.ports[index];
        const auto& b = right.ports[index];
        CHECK_EQ(a.controller.connected, b.controller.connected);
        CHECK_EQ(a.controller.device, b.controller.device);
        CHECK_EQ(a.controller.accessory, b.controller.accessory);
        CHECK_EQ(b.controller.buttons, persisted ? 0U : a.controller.buttons);
        CHECK_EQ(b.controller.stick_x, persisted ? 0 : a.controller.stick_x);
        CHECK_EQ(b.controller.stick_y, persisted ? 0 : a.controller.stick_y);
        CHECK_EQ(a.controller_selected, b.controller_selected);
        CHECK_EQ(a.accessory_selected, b.accessory_selected);
        CHECK_EQ(a.pak_banks_selected, b.pak_banks_selected);
        CHECK_EQ(a.pak_banks, b.pak_banks);
        check_paths_equal(a.pak_file, b.pak_file);
        check_paths_equal(a.transfer.cartridge, b.transfer.cartridge);
        check_paths_equal(a.transfer.save_file, b.transfer.save_file);
        check_paths_equal(a.transfer.rtc_file, b.transfer.rtc_file);
        CHECK_EQ(a.transfer.mapper, b.transfer.mapper);
        CHECK_EQ(a.transfer.ram_bytes, b.transfer.ram_bytes);
        CHECK_EQ(a.transfer.clock, b.transfer.clock);
        CHECK_EQ(a.transfer.rumble, b.transfer.rumble);
    }
}

Preferences detailed_preferences(const test::host::TempDirectory& directory) {
    Preferences preferences;
    preferences.volume = 0.375F;
    preferences.muted = true;
    preferences.input_bindings = "pad0=\"A B\"\\naxis=測試\\\\left";
    auto& hardware = preferences.hardware;
    hardware.cartridge = directory.path() / std::filesystem::path(u8"ROMs/測試 game.z64");
    hardware.pif = directory.path() / std::filesystem::path(u8"firmware/啟動 image.rom");
    hardware.save_file = directory.path() / std::filesystem::path(u8"saves/main \"quoted\".fla");
    hardware.rtc_file = directory.path() / std::filesystem::path(u8"saves/clock state.rtc");
    hardware.region = VideoStandard::Pal;
    hardware.pif_region = VideoStandard::Pal;
    hardware.ram_mib = 4;
    hardware.save = SaveType::FlashRam;
    hardware.sram_bytes = 128U * 1024U;
    hardware.flash_chip = FlashChip::Mx29L1101C;
    hardware.cic = CicModel::Nus7102;
    hardware.rtc = true;

    hardware.ports[0].controller = {
        true, 0x1234, 12, -9, ControllerAccessory::ControllerPak, ControllerDevice::Gamepad};
    hardware.ports[0].controller_selected = true;
    hardware.ports[0].accessory_selected = true;
    hardware.ports[0].pak_file = directory.path() / std::filesystem::path(u8"paks/player one.pak");

    hardware.ports[1].controller = {
        true, 0xffff, -4, 7, ControllerAccessory::TransferPak, ControllerDevice::Gamepad};
    hardware.ports[1].controller_selected = true;
    hardware.ports[1].accessory_selected = true;
    hardware.ports[1].transfer.cartridge = directory.path() / std::filesystem::path(u8"gb/ポケ game.gb");
    hardware.ports[1].transfer.save_file = directory.path() / std::filesystem::path(u8"gb/save data.sav");
    hardware.ports[1].transfer.rtc_file = directory.path() / std::filesystem::path(u8"gb/clock data.gbrt");
    hardware.ports[1].transfer.mapper = GameBoyMapper::Mbc3;
    hardware.ports[1].transfer.ram_bytes = 128U * 1024U;
    hardware.ports[1].transfer.clock = true;
    hardware.ports[1].transfer.rumble = false;

    hardware.ports[2].controller = {true, 1, 1, 1, ControllerAccessory::None, ControllerDevice::Mouse};
    hardware.ports[2].controller_selected = true;
    hardware.ports[3].controller = {false, 2, 2, 2, ControllerAccessory::None, ControllerDevice::Gamepad};
    return preferences;
}

System storage_system(std::span<const u8> rom, SaveType save = SaveType::Sram) {
    System system;
    system.bus.rom.assign(rom.begin(), rom.end());
    system.bus.set_save_type(save);
    return system;
}

std::filesystem::path expected_directory(const std::filesystem::path& root, std::string_view digest) {
    return root / ("n64-" + std::string(digest));
}

std::vector<u8> byte_swapped(std::vector<u8> rom) {
    for (std::size_t offset = 0; offset < rom.size(); offset += 2)
        std::swap(rom[offset], rom[offset + 1]);
    return rom;
}

void insert_game_boy(System& system, unsigned port, std::vector<u8> rom, GameBoyCartridgeConfig config) {
    std::string error;
    auto cartridge = GameBoyCartridge::create(std::move(rom), config, error);
    CHECK(cartridge.has_value());
    system.bus.transfer_paks[port].insert(std::move(*cartridge));
}

} // namespace

TEST(host_preferences_roundtrip_preserves_explicit_hardware_utf8_spaces_and_escapes) {
    test::host::TempDirectory directory;
    const auto path = directory.path() / std::filesystem::path(u8"prefs 測試 file.cfg");
    const auto saved = detailed_preferences(directory);
    std::string error;
    CHECK(save_preferences(path, saved, error));

    Preferences loaded;
    loaded.volume = 0.99F;
    CHECK(load_preferences(path, loaded, error));
    CHECK_EQ(loaded.volume, saved.volume);
    CHECK_EQ(loaded.muted, saved.muted);
    CHECK_EQ(loaded.input_bindings, saved.input_bindings);
    check_options_equal(saved.hardware, loaded.hardware, true);
    CHECK_EQ(loaded.hardware.max_instructions, 4000000000ULL);
    CHECK(!loaded.hardware.require_success);
    CHECK(!loaded.hardware.require_extended);
    CHECK(!loaded.hardware.help);
}

TEST(host_preferences_missing_file_succeeds_without_mutating_existing_preferences) {
    test::host::TempDirectory directory;
    Preferences preferences = detailed_preferences(directory);
    const auto before = preferences;
    std::string error = "stale";
    CHECK(load_preferences(directory.path() / "missing.cfg", preferences, error));
    CHECK(error.empty());
    CHECK_EQ(preferences.volume, before.volume);
    CHECK_EQ(preferences.muted, before.muted);
    CHECK_EQ(preferences.input_bindings, before.input_bindings);
    check_options_equal(before.hardware, preferences.hardware);
}

TEST(host_preferences_preserve_controller_pak_bank_capacity_across_restart) {
    test::host::TempDirectory directory;
    const auto path = directory.path() / "banked.cfg";
    Preferences saved;
    auto& port = saved.hardware.ports[0];
    port.controller = {true, 0, 0, 0, ControllerAccessory::ControllerPak, ControllerDevice::Gamepad};
    port.controller_selected = port.accessory_selected = port.pak_banks_selected = true;
    port.pak_banks = 62;
    port.pak_file = directory.path() / "large.pak";
    std::string error;
    CHECK(save_preferences(path, saved, error));
    Preferences restored;
    CHECK(load_preferences(path, restored, error));
    CHECK_EQ(restored.hardware.ports[0].pak_banks, 62U);
    CHECK(restored.hardware.ports[0].pak_banks_selected);
    CHECK_EQ(restored.hardware.ports[0].pak_file, port.pak_file);
}

TEST(host_preferences_preserve_gamecube_controller_selection_across_restart) {
    test::host::TempDirectory directory;
    const auto path = directory.path() / "gamecube.cfg";
    Preferences saved;
    saved.hardware.ports[0].controller = {
        true, 0, 0, 0, ControllerAccessory::None, ControllerDevice::GameCube};
    saved.hardware.ports[0].controller_selected = true;
    std::string error;
    CHECK(save_preferences(path, saved, error));
    Preferences restored;
    CHECK(load_preferences(path, restored, error));
    CHECK_EQ(restored.hardware.ports[0].controller.device, ControllerDevice::GameCube);
    CHECK(restored.hardware.ports[0].controller.connected);
    CHECK(restored.hardware.ports[0].controller_selected);
}

TEST(host_preferences_load_version_one_with_original_controller_pak_capacity) {
    test::host::TempDirectory directory;
    std::string payload = "volume_millionths=250000\nmuted=1\ninput_bindings=\"\"\n"
                          "cartridge=\"legacy cart.z64\"\npif=\"pif.rom\"\nsave_file=\"\"\nrtc_file=\"\"\n"
                          "region=ntsc\npif_region=auto\nram_mib=8\nsave=eeprom4k\nsram_bytes=32768\n"
                          "flash_chip=auto\ncic=auto\nrtc=0\n";
    // This is the original ordered v1 payload, which has no bank fields.
    for (unsigned port = 1; port <= 4; ++port) {
        const auto prefix = "port" + std::to_string(port) + '_';
        payload += prefix + "connected=1\n" + prefix + "device=gamepad\n" + prefix +
                   "accessory=controller-pak\n" + prefix + "controller_selected=1\n" + prefix +
                   "accessory_selected=1\n" + prefix + "pak_file=\"\"\n" + prefix + "transfer_rom=\"\"\n" +
                   prefix + "transfer_save=\"\"\n" + prefix + "transfer_rtc_file=\"\"\n" + prefix +
                   "transfer_mapper=auto\n" + prefix + "transfer_ram=auto\n" + prefix +
                   "transfer_rtc=auto\n" + prefix + "transfer_rumble=auto\n";
    }
    const auto encoded = "CUPID-PREFERENCES 1 " + std::to_string(payload.size()) + '\n' + payload;
    const auto path = directory.write("legacy.cfg", std::vector<u8>(encoded.begin(), encoded.end()));
    Preferences restored;
    restored.hardware.ports[0].pak_banks = 62;
    restored.hardware.ports[0].pak_banks_selected = true;
    std::string error;
    CHECK(load_preferences(path, restored, error));
    CHECK_EQ(restored.volume, 0.25F);
    CHECK(restored.muted);
    CHECK_EQ(path_text(restored.hardware.cartridge), "legacy cart.z64");
    CHECK_EQ(restored.hardware.save, SaveType::Eeprom4K);
    for (const auto& port : restored.hardware.ports) {
        CHECK_EQ(port.pak_banks, 1U);
        CHECK(!port.pak_banks_selected);
        CHECK_EQ(port.controller.accessory, ControllerAccessory::ControllerPak);
    }
    CHECK(save_preferences(path, restored, error));
    const auto upgraded = read_bytes(path);
    CHECK(std::string(upgraded.begin(), upgraded.end()).starts_with("CUPID-PREFERENCES 2 "));
    Preferences reloaded;
    CHECK(load_preferences(path, reloaded, error));
    check_options_equal(restored.hardware, reloaded.hardware, true);
}

TEST(host_preferences_reject_invalid_controller_pak_banks_without_replacing_settings) {
    test::host::TempDirectory directory;
    const auto path = directory.path() / "banks.cfg";
    Preferences saved;
    saved.hardware.ports[0].pak_banks = 62;
    saved.hardware.ports[0].pak_banks_selected = true;
    std::string error;
    CHECK(save_preferences(path, saved, error));
    const auto valid = read_bytes(path);
    for (const unsigned banks : {0U, 63U}) {
        saved.hardware.ports[0].pak_banks = banks;
        CHECK(!save_preferences(path, saved, error));
        CHECK_EQ(read_bytes(path), valid);
    }
    auto invalid = valid;
    const std::string token = "port1_pak_banks=62\n";
    const auto found = std::search(invalid.begin(), invalid.end(), token.begin(), token.end());
    CHECK(found != invalid.end());
    found[static_cast<std::ptrdiff_t>(token.size()) - 2] = '3';
    const auto invalid_path = directory.write("invalid-banks.cfg", invalid);
    Preferences restored;
    restored.volume = 0.125F;
    CHECK(!load_preferences(invalid_path, restored, error));
    CHECK_EQ(restored.volume, 0.125F);
    CHECK_EQ(restored.hardware.ports[0].pak_banks, 1U);
}

TEST(host_preferences_reject_unknown_version_length_enum_and_malformed_data_without_mutating) {
    test::host::TempDirectory directory;
    const auto valid_path = directory.path() / "valid.cfg";
    const auto source = detailed_preferences(directory);
    std::string error;
    CHECK(save_preferences(valid_path, source, error));
    const auto valid = read_bytes(valid_path);

    const auto check_rejected = [&](std::string_view name, std::vector<u8> bytes) {
        const auto path = directory.write(name, bytes);
        Preferences destination = source;
        destination.volume = 0.125F;
        destination.input_bindings = "unchanged";
        CHECK(!load_preferences(path, destination, error));
        CHECK_EQ(destination.volume, 0.125F);
        CHECK_EQ(destination.input_bindings, "unchanged");
        check_options_equal(source.hardware, destination.hardware);
    };

    auto unknown = valid;
    const std::string version_token = "CUPID-PREFERENCES 2 ";
    const auto version =
        std::search(unknown.begin(), unknown.end(), version_token.begin(), version_token.end());
    CHECK(version != unknown.end());
    version[18] = '9';
    check_rejected("unknown.cfg", unknown);

    auto wrong_length = valid;
    const auto newline = std::find(wrong_length.begin(), wrong_length.end(), static_cast<u8>('\n'));
    CHECK(newline != wrong_length.end());
    auto last_digit = newline;
    --last_digit;
    CHECK(*last_digit >= '0' && *last_digit <= '9');
    *last_digit = *last_digit == '9' ? '8' : static_cast<u8>(*last_digit + 1U);
    check_rejected("length.cfg", wrong_length);

    auto bad_enum = valid;
    const std::string enum_text = "save=flash";
    const auto enum_pos = std::search(bad_enum.begin(), bad_enum.end(), enum_text.begin(), enum_text.end());
    CHECK(enum_pos != bad_enum.end());
    std::copy_n("xxxxx", 5, enum_pos + 5);
    check_rejected("enum.cfg", bad_enum);

    const std::string path_token = "cartridge=\"";
    const auto path_position = std::search(valid.begin(), valid.end(), path_token.begin(), path_token.end());
    CHECK(path_position != valid.end());

    auto malformed_utf8 = valid;
    malformed_utf8[static_cast<std::size_t>(path_position - valid.begin()) + path_token.size()] = 0xff;
    check_rejected("malformed-utf8.cfg", malformed_utf8);
    CHECK(error.find("UTF-8") != std::string::npos);

    auto embedded_nul = valid;
    embedded_nul[static_cast<std::size_t>(path_position - valid.begin()) + path_token.size()] = 0;
    check_rejected("embedded-nul.cfg", embedded_nul);
    CHECK(error.find("NUL") != std::string::npos);

    check_rejected("malformed.cfg", std::vector<u8>{'C', 'U', 'P', 'I', 'D', '\n'});
}

TEST(host_preferences_reject_oversize_bindings_files_and_nonfinite_volume) {
    test::host::TempDirectory directory;
    std::string error;
    Preferences preferences;
    preferences.input_bindings.assign(16U * 1024U + 1U, 'x');
    CHECK(!save_preferences(directory.path() / "bindings.cfg", preferences, error));
    CHECK(error.find("16 KiB") != std::string::npos);

    preferences.input_bindings.clear();
    preferences.volume = std::numeric_limits<float>::quiet_NaN();
    CHECK(!save_preferences(directory.path() / "nan.cfg", preferences, error));
    CHECK(error.find("finite") != std::string::npos);

    const std::vector<u8> oversized(512U * 1024U + 1U, 'x');
    const auto oversized_path = directory.write("oversized.cfg", oversized);
    Preferences destination;
    destination.volume = 0.25F;
    CHECK(!load_preferences(oversized_path, destination, error));
    CHECK_EQ(destination.volume, 0.25F);
    CHECK(error.find("512 KiB") != std::string::npos);
}

TEST(host_preferences_reject_embedded_nul_path_on_save) {
    test::host::TempDirectory directory;
    Preferences preferences;
    const std::string with_nul{"rom\0hidden.z64", 14};
    preferences.hardware.cartridge = argument_path(with_nul);
    std::string error;
    CHECK(!save_preferences(directory.path() / "preferences.cfg", preferences, error));
    CHECK(error.find("NUL") != std::string::npos);
}

TEST(host_preferences_serialized_header_and_payload_share_512kib_limit_and_preserve_previous_file) {
    test::host::TempDirectory directory;
    const auto path = directory.path() / "preferences.cfg";
    std::string error;

    Preferences large;
    large.hardware.save = SaveType::Sram;
    large.hardware.rtc = true;
    CHECK(save_preferences(path, large, error));
    const auto original = read_bytes(path);
    const std::size_t baseline_payload = encoded_payload_size(original);
    constexpr std::size_t limit = 512U * 1024U;
    CHECK(baseline_payload < limit);

    std::array<std::filesystem::path*, 20> paths{
        &large.hardware.cartridge,
        &large.hardware.pif,
        &large.hardware.save_file,
        &large.hardware.rtc_file,
        &large.hardware.ports[0].pak_file,
        &large.hardware.ports[0].transfer.cartridge,
        &large.hardware.ports[0].transfer.save_file,
        &large.hardware.ports[0].transfer.rtc_file,
        &large.hardware.ports[1].pak_file,
        &large.hardware.ports[1].transfer.cartridge,
        &large.hardware.ports[1].transfer.save_file,
        &large.hardware.ports[1].transfer.rtc_file,
        &large.hardware.ports[2].pak_file,
        &large.hardware.ports[2].transfer.cartridge,
        &large.hardware.ports[2].transfer.save_file,
        &large.hardware.ports[2].transfer.rtc_file,
        &large.hardware.ports[3].pak_file,
        &large.hardware.ports[3].transfer.cartridge,
        &large.hardware.ports[3].transfer.save_file,
        &large.hardware.ports[3].transfer.rtc_file,
    };

    std::size_t remaining = limit - baseline_payload;
    for (auto* candidate : paths) {
        const std::size_t tabs = std::min<std::size_t>(remaining / 2U, 16U * 1024U);
        *candidate = argument_path(std::string(tabs, '\t'));
        remaining -= tabs * 2U;
    }
    if (remaining != 0) {
        CHECK_EQ(remaining, 1U);
        auto text = path_text(*paths.back());
        CHECK(text.size() < 16U * 1024U);
        text.push_back('a');
        *paths.back() = argument_path(text);
        remaining = 0;
    }
    CHECK_EQ(remaining, 0U);

    CHECK(!save_preferences(path, large, error));
    CHECK(error.find("512 KiB") != std::string::npos);
    CHECK_EQ(read_bytes(path), original);
}

TEST(host_preferences_save_destination_cannot_collide_with_rom_firmware_or_device_storage) {
    test::host::TempDirectory directory;
    const std::vector<u8> sentinel{'n', 'o', 't', '-', 'p', 'r', 'e', 'f', 's'};
    std::string error;

    const auto check_collision = [&](std::string_view name, auto select_path) {
        const auto collision = directory.write(name, sentinel);
        Preferences preferences;
        preferences.hardware.save = SaveType::Sram;
        preferences.hardware.rtc = true;
        select_path(preferences, collision);
        CHECK(!save_preferences(collision, preferences, error));
        CHECK(error.find("collides") != std::string::npos);
        CHECK_EQ(read_bytes(collision), sentinel);
    };

    check_collision("cartridge.z64",
                    [](Preferences& value, const auto& path) { value.hardware.cartridge = path; });
    check_collision("pif.rom", [](Preferences& value, const auto& path) { value.hardware.pif = path; });
    check_collision("save.sra",
                    [](Preferences& value, const auto& path) { value.hardware.save_file = path; });
    check_collision("clock.rtc",
                    [](Preferences& value, const auto& path) { value.hardware.rtc_file = path; });
    check_collision("controller.pak",
                    [](Preferences& value, const auto& path) { value.hardware.ports[0].pak_file = path; });
    check_collision("game.gb", [](Preferences& value, const auto& path) {
        value.hardware.ports[0].transfer.cartridge = path;
    });
    check_collision("game.sav", [](Preferences& value, const auto& path) {
        value.hardware.ports[0].transfer.save_file = path;
    });
    check_collision("game.gbrt", [](Preferences& value, const auto& path) {
        value.hardware.ports[0].transfer.rtc_file = path;
    });
}

TEST(host_preferences_failed_atomic_write_preserves_previous_preferences_file) {
    test::host::TempDirectory directory;
    const auto path = directory.path() / "preferences.cfg";
    std::string error;
    Preferences original;
    original.volume = 0.25F;
    original.input_bindings = "old";
    CHECK(save_preferences(path, original, error));
    const auto before = read_bytes(path);

    for (unsigned index = 0; index < 100; ++index) {
        auto occupied = path;
        occupied += ".tmp." + std::to_string(index);
        CHECK(std::filesystem::create_directory(occupied));
    }
    Preferences replacement = original;
    replacement.volume = 0.75F;
    replacement.input_bindings = "new";
    CHECK(!save_preferences(path, replacement, error));
    CHECK_EQ(read_bytes(path), before);
}

TEST(host_preferences_sha256_matches_standard_vectors_through_content_directory) {
    test::host::TempDirectory directory;
    std::string error;
    Options options;

    const std::array<u8, 3> abc{'a', 'b', 'c'};
    auto first = storage_system(abc);
    CHECK(prepare_storage_paths(first, options, directory.path(), error));
    CHECK_EQ(options.save_file.parent_path(),
             expected_directory(directory.path(),
                                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    const std::string second_text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    std::vector<u8> second_bytes(second_text.begin(), second_text.end());
    auto second = storage_system(second_bytes);
    Options second_options;
    CHECK(prepare_storage_paths(second, second_options, directory.path(), error));
    CHECK_EQ(second_options.save_file.parent_path(),
             expected_directory(directory.path(),
                                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(host_preferences_normalized_n64_content_controls_automatic_storage_identity) {
    test::host::TempDirectory directory;
    const auto canonical = test::host::n64_rom();
    const auto native_path = directory.write("same.z64", canonical);
    const auto swapped_path = directory.write("same.v64", byte_swapped(canonical));
    std::string error;
    std::vector<u8> normalized_native, normalized_swapped;
    CHECK(read_media(native_path, MediaKind::Cartridge, normalized_native, error));
    CHECK(read_media(swapped_path, MediaKind::Cartridge, normalized_swapped, error));
    CHECK_EQ(normalized_native, normalized_swapped);

    auto first = storage_system(normalized_native);
    auto second = storage_system(normalized_swapped);
    Options first_options, second_options;
    CHECK(prepare_storage_paths(first, first_options, directory.path(), error));
    CHECK(prepare_storage_paths(second, second_options, directory.path(), error));
    CHECK_EQ(first_options.save_file, second_options.save_file);

    normalized_swapped.back() ^= 1U;
    auto different = storage_system(normalized_swapped);
    Options different_options;
    CHECK(prepare_storage_paths(different, different_options, directory.path(), error));
    CHECK(first_options.save_file != different_options.save_file);
}

TEST(host_preferences_prepare_storage_paths_fills_only_empty_paths_for_actual_devices) {
    test::host::TempDirectory directory;
    const std::array<u8, 3> rom{'a', 'b', 'c'};
    auto system = storage_system(rom, SaveType::None);
    for (unsigned port = 0; port < 4; ++port)
        system.bus.set_controller_state(
            port, {false, 0, 0, 0, ControllerAccessory::None, ControllerDevice::Gamepad});
    Options options;
    const auto explicit_save = directory.path() / "explicit-save.bin";
    options.save_file = explicit_save;
    std::string error;
    CHECK(prepare_storage_paths(system, options, directory.path(), error));
    CHECK_EQ(options.save_file, explicit_save);
    CHECK(options.rtc_file.empty());
    for (const auto& port : options.ports) {
        CHECK(port.pak_file.empty());
        CHECK(port.transfer.save_file.empty());
        CHECK(port.transfer.rtc_file.empty());
    }

    system.bus.set_save_type(SaveType::Eeprom16K);
    CartridgeRtc::Registers registers{};
    system.bus.rtc.emplace(registers);
    system.bus.set_controller_state(
        0, {true, 0, 0, 0, ControllerAccessory::ControllerPak, ControllerDevice::Gamepad});
    options.save_file.clear();
    CHECK(prepare_storage_paths(system, options, directory.path(), error));
    CHECK_EQ(options.save_file.extension(), ".eep");
    CHECK_EQ(options.rtc_file.filename(), "cartridge.rtc");
    CHECK_EQ(options.ports[0].pak_file.filename(), "controller-pak-1.pak");
    CHECK(options.ports[1].pak_file.empty());
}

TEST(host_preferences_transfer_pak_identity_uses_actual_game_boy_rom_contents) {
    test::host::TempDirectory directory_one("-one");
    test::host::TempDirectory directory_two("-two");
    test::host::TempDirectory storage("-storage");
    const std::array<u8, 3> n64_rom{'n', '6', '4'};

    auto first_rom = test::host::game_boy_rom(0x10);
    auto second_rom = first_rom;
    second_rom[0x200] ^= 0x5a;
    const auto first_path = directory_one.write("same.gb", first_rom);
    const auto second_path = directory_two.write("same.gb", second_rom);

    auto first = storage_system(n64_rom, SaveType::None);
    auto second = storage_system(n64_rom, SaveType::None);
    const GameBoyCartridgeConfig config{GameBoyMapper::Mbc3, 0x8000, true, false};
    insert_game_boy(first, 0, first_rom, config);
    insert_game_boy(second, 0, second_rom, config);

    Options first_options, second_options;
    first_options.ports[0].transfer.cartridge = first_path;
    second_options.ports[0].transfer.cartridge = second_path;
    std::string error;
    CHECK(prepare_storage_paths(first, first_options, storage.path(), error));
    CHECK(prepare_storage_paths(second, second_options, storage.path(), error));
    CHECK(first_options.ports[0].transfer.save_file != second_options.ports[0].transfer.save_file);
    CHECK(first_options.ports[0].transfer.rtc_file != second_options.ports[0].transfer.rtc_file);

    Options same_contents;
    same_contents.ports[0].transfer.cartridge = directory_two.write("copy.gb", first_rom);
    CHECK(prepare_storage_paths(first, same_contents, storage.path(), error));
    CHECK_EQ(first_options.ports[0].transfer.save_file, same_contents.ports[0].transfer.save_file);

    auto reference = storage_system(n64_rom, SaveType::None);
    insert_game_boy(reference, 0, first_rom, config);
    Options reference_options;
    reference_options.ports[0].transfer.cartridge = same_contents.ports[0].transfer.cartridge;
    CHECK(prepare_storage_paths(reference, reference_options, storage.path(), error));

    directory_one.write("same.gb", second_rom);
    Options replaced_source;
    replaced_source.ports[0].transfer.cartridge = first_path;
    CHECK(prepare_storage_paths(first, replaced_source, storage.path(), error));
    CHECK_EQ(replaced_source.ports[0].transfer.save_file, reference_options.ports[0].transfer.save_file);
    CHECK_EQ(replaced_source.ports[0].transfer.rtc_file, reference_options.ports[0].transfer.rtc_file);

    CHECK(std::filesystem::remove(first_path));
    Options removed_source;
    removed_source.ports[0].transfer.cartridge = first_path;
    CHECK(prepare_storage_paths(first, removed_source, storage.path(), error));
    CHECK_EQ(removed_source.ports[0].transfer.save_file, reference_options.ports[0].transfer.save_file);
    CHECK_EQ(removed_source.ports[0].transfer.rtc_file, reference_options.ports[0].transfer.rtc_file);

    Options changed_source;
    changed_source.ports[0].transfer.cartridge = second_path;
    CHECK(prepare_storage_paths(first, changed_source, storage.path(), error));
    CHECK_EQ(changed_source.ports[0].transfer.save_file, reference_options.ports[0].transfer.save_file);
    CHECK_EQ(changed_source.ports[0].transfer.rtc_file, reference_options.ports[0].transfer.rtc_file);

    Options no_source_path;
    CHECK(prepare_storage_paths(first, no_source_path, storage.path(), error));
    CHECK_EQ(no_source_path.ports[0].transfer.save_file, reference_options.ports[0].transfer.save_file);
    CHECK_EQ(no_source_path.ports[0].transfer.rtc_file, reference_options.ports[0].transfer.rtc_file);
}

TEST(host_preferences_prepare_storage_paths_rejects_conflicts_and_preserves_options_on_failure) {
    test::host::TempDirectory directory;
    const std::array<u8, 3> rom{'a', 'b', 'c'};
    auto system = storage_system(rom);
    CartridgeRtc::Registers registers{};
    system.bus.rtc.emplace(registers);
    const auto content = expected_directory(
        directory.path(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    Options options;
    options.save_file = content / "cartridge.rtc";
    const auto before = options;
    std::string error;
    CHECK(!prepare_storage_paths(system, options, directory.path(), error));
    check_options_equal(before, options);
    CHECK(error.find("collides") != std::string::npos);

    Options unavailable;
    const auto root_file = directory.write("not-a-directory", std::vector<u8>{1, 2, 3});
    const auto unavailable_before = unavailable;
    CHECK(!prepare_storage_paths(system, unavailable, root_file, error));
    check_options_equal(unavailable_before, unavailable);
}
