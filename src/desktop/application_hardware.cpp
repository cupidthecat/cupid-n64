#include "application_internal.hpp"

#include <array>

namespace cupid::desktop {
namespace {

std::string_view save_name(std::optional<SaveType> type, unsigned bytes) {
    if (!type)
        return "Choose save type";
    switch (*type) {
    case SaveType::None:
        return "No save device";
    case SaveType::Eeprom4K:
        return "EEPROM 4 Kbit";
    case SaveType::Eeprom16K:
        return "EEPROM 16 Kbit";
    case SaveType::Sram:
        return bytes == 98304 ? "SRAM 96 KiB" : bytes == 131072 ? "SRAM 128 KiB" : "SRAM 32 KiB";
    case SaveType::FlashRam:
        return "FlashRAM";
    }
    return "Unknown save type";
}

std::string_view region_name(std::optional<VideoStandard> region) {
    return !region ? "Auto" : *region == VideoStandard::Pal ? "PAL" : "NTSC";
}

void next_region(std::optional<VideoStandard>& region) {
    region = !region                          ? std::optional{VideoStandard::Ntsc}
             : *region == VideoStandard::Ntsc ? std::optional{VideoStandard::Pal}
                                              : std::nullopt;
}

constexpr std::array<std::string_view, 11> cic_names{"6101", "6102", "7102", "6103", "6105", "6106",
                                                     "5101", "5167", "8303", "8401", "ddus"};
constexpr std::array<std::string_view, 7> flash_names{"MX29L0000",  "MX29L0001",  "MX29L1100", "MX29L1101A",
                                                      "MX29L1101B", "MX29L1101C", "MN63F81MPN"};
constexpr std::array<std::string_view, 5> accessory_names{"None", "Controller Pak", "Rumble Pak",
                                                          "Bio Sensor", "Transfer Pak"};

} // namespace

void Application::draw_hardware() {
    auto& hardware = preferences.hardware;
    button(12, 51, 96, "Cartridge", [this] { open_file(FileKind::Cartridge); }, !dialogs.pending());
    text(118, 59, fit_text(host::path_text(hardware.cartridge), 53));
    button(12, 83, 96, "Firmware", [this] { open_file(FileKind::Firmware); }, !dialogs.pending());
    text(118, 91, fit_text(host::path_text(hardware.pif), 53));
    button(12, 121, 174, std::string(save_name(hardware.save, hardware.sram_bytes)), [this] {
        auto& selected = preferences.hardware;
        if (!selected.save)
            selected.save = SaveType::None;
        else if (*selected.save == SaveType::None)
            selected.save = SaveType::Eeprom4K;
        else if (*selected.save == SaveType::Eeprom4K)
            selected.save = SaveType::Eeprom16K;
        else if (*selected.save == SaveType::Eeprom16K) {
            selected.save = SaveType::Sram;
            selected.sram_bytes = 32768;
        } else if (*selected.save == SaveType::Sram && selected.sram_bytes < 131072) {
            selected.sram_bytes = selected.sram_bytes == 32768 ? 98304 : 131072;
        } else if (*selected.save == SaveType::Sram) {
            selected.save = SaveType::FlashRam;
            selected.flash_chip = FlashChip::Mx29L1100;
        } else {
            selected.save = SaveType::None;
        }
        if (selected.save != SaveType::FlashRam)
            selected.flash_chip.reset();
        selected.save_file.clear();
    });
    button(192, 121, 90, "RAM " + std::to_string(hardware.ram_mib) + " MiB",
           [this] { preferences.hardware.ram_mib = preferences.hardware.ram_mib == 8 ? 4 : 8; });
    button(288, 121, 122, "Region " + std::string(region_name(hardware.region)),
           [this] { next_region(preferences.hardware.region); });
    button(416, 121, 132, "PIF " + std::string(region_name(hardware.pif_region)),
           [this] { next_region(preferences.hardware.pif_region); });
    const auto cic = hardware.cic ? cic_names.at(static_cast<std::size_t>(*hardware.cic)) : "Auto";
    button(12, 154, 126, "CIC " + std::string(cic), [this] {
        auto& selected = preferences.hardware.cic;
        if (!selected)
            selected = CicModel::Nus6101;
        else if (static_cast<std::size_t>(*selected) + 1 == cic_names.size())
            selected.reset();
        else
            selected = static_cast<CicModel>(static_cast<unsigned>(*selected) + 1);
    });
    const auto flash =
        hardware.flash_chip ? flash_names.at(static_cast<std::size_t>(*hardware.flash_chip)) : "No FlashRAM";
    button(
        144, 154, 254, "Chip " + std::string(flash),
        [this] {
            auto& chip = preferences.hardware.flash_chip;
            chip = static_cast<FlashChip>(
                (static_cast<std::size_t>(chip.value_or(FlashChip::Mx29L1100)) + 1) % flash_names.size());
        },
        hardware.save == SaveType::FlashRam);
    button(404, 154, 144, hardware.rtc ? "RTC attached" : "RTC absent", [this] {
        preferences.hardware.rtc = !preferences.hardware.rtc;
        if (!preferences.hardware.rtc)
            preferences.hardware.rtc_file.clear();
    });
    auto& port = hardware.ports[input_port];
    button(12, 201, 90, "Port " + std::to_string(input_port + 1),
           [this] { input_port = (input_port + 1) % 4; });
    button(108, 201, 138, port.controller.connected ? "Gamepad present" : "Port disconnected", [this] {
        auto& selected = preferences.hardware.ports[input_port];
        selected.controller.connected = !selected.controller.connected;
        selected.controller.device = ControllerDevice::Gamepad;
        selected.controller_selected = selected.accessory_selected = true;
        if (!selected.controller.connected) {
            selected.controller.accessory = ControllerAccessory::None;
            selected.pak_file.clear();
            selected.transfer = {};
        }
    });
    button(
        252, 201, 296,
        "Accessory " + std::string(accessory_names.at(static_cast<std::size_t>(port.controller.accessory))),
        [this] {
            auto& selected = preferences.hardware.ports[input_port];
            selected.controller.accessory = static_cast<ControllerAccessory>(
                (static_cast<unsigned>(selected.controller.accessory) + 1) % accessory_names.size());
            selected.accessory_selected = true;
            if (selected.controller.accessory != ControllerAccessory::ControllerPak)
                selected.pak_file.clear();
            if (selected.controller.accessory != ControllerAccessory::TransferPak)
                selected.transfer = {};
        },
        port.controller.connected);
    button(
        12, 234, 142, "Load Pak image", [this] { open_file(FileKind::ControllerPak, input_port); },
        port.controller.connected && !dialogs.pending());
    button(
        160, 234, 142, "Load GB image", [this] { open_file(FileKind::TransferCartridge, input_port); },
        port.controller.connected && !dialogs.pending());
    button(308, 234, 240, "Use automatic storage paths", [this] {
        auto& selected = preferences.hardware;
        selected.save_file.clear();
        selected.rtc_file.clear();
        for (auto& current : selected.ports) {
            current.pak_file.clear();
            current.transfer.save_file.clear();
            current.transfer.rtc_file.clear();
        }
        message = "Save files will be selected by cartridge content when loaded.";
    });
    const auto media = port.controller.accessory == ControllerAccessory::TransferPak ? port.transfer.cartridge
                                                                                     : port.pak_file;
    text(12, 267,
         fit_text(media.empty() ? "No accessory image selected; new Pak contents use automatic storage."
                                : host::path_text(media),
                  67),
         true);
    text(12, 286, "Hardware changes take effect when the cartridge is loaded.", true);
    button(12, 311, 126, "Load cartridge", [this] { load_game(); }, !dialogs.pending() && load_request == 0);
    button(144, 311, 126, "Save settings", [this] { save_settings(); });
    text(284, 319, "Click each setting to cycle it.", true);
}

} // namespace cupid::desktop
