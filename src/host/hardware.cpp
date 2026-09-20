#include "cupid/host/hardware.hpp"

#include "cupid/host/media.hpp"
#include "cupid/host/storage.hpp"

#include <array>
#include <sstream>

namespace cupid::host {
namespace {

std::optional<SaveType> save_type(const Options& options, std::span<const u8> cartridge, std::string& error) {
    static_cast<void>(cartridge);
    if (options.save)
        return options.save;
    error = "Cartridge save hardware is ambiguous. Select --save none, sram32, sram96, sram128, flash, "
            "eeprom4k, or eeprom16k.";
    return std::nullopt;
}

bool configure_storage(System& system, const Options& options, SaveType save, std::string& error) {
    if (save == SaveType::None && !options.save_file.empty()) {
        error = "A save file was supplied, but no cartridge save hardware is selected.";
        return false;
    }
    if (save == SaveType::FlashRam && !options.flash_chip) {
        error = "FlashRAM requires an explicit --flash-chip selection.";
        return false;
    }
    if (save == SaveType::Sram) {
        if (options.sram_bytes != 32U * 1024U && options.sram_bytes != 96U * 1024U &&
            options.sram_bytes != 128U * 1024U) {
            error = "SRAM capacity must be 32, 96, or 128 KiB.";
            return false;
        }
        system.bus.sram.assign(options.sram_bytes, 0);
    }
    system.bus.set_save_type(save);
    if (options.flash_chip)
        system.bus.set_flash_chip(*options.flash_chip);
    if (options.rtc) {
        CartridgeRtc::Registers registers{};
        registers[18] = 0x80;
        registers[19] = 1;
        registers[20] = 6;
        registers[21] = 1;
        registers[23] = 0x20;
        system.bus.rtc.emplace(registers);
    }
    return true;
}

bool configure_ports(System& system, const Options& options, std::string& error) {
    for (unsigned index = 0; index < options.ports.size(); ++index) {
        const auto& port = options.ports[index];
        system.bus.set_controller_state(index, port.controller);
        if (port.transfer.cartridge.empty())
            continue;
        std::vector<u8> data;
        if (!read_media(port.transfer.cartridge, MediaKind::GameBoy, data, error))
            return false;
        const auto config = game_boy_configuration(data, port.transfer, error);
        if (!config)
            return false;
        auto cartridge = GameBoyCartridge::create(std::move(data), *config, error);
        if (!cartridge)
            return false;
        system.bus.transfer_paks[index].insert(std::move(*cartridge));
    }
    return true;
}

} // namespace

std::unique_ptr<System> create_system(const Options& options, std::string& error) {
    error.clear();
    if (!validate_storage_paths(options, error))
        return nullptr;
    std::vector<u8> cartridge;
    std::vector<u8> firmware;
    if (!read_media(options.cartridge, MediaKind::Cartridge, cartridge, error) ||
        !read_media(options.pif, MediaKind::Pif, firmware, error))
        return nullptr;
    const auto cart_region = header_region(cartridge);
    auto boot_region = firmware_region(firmware);
    if (boot_region && options.pif_region && boot_region != options.pif_region) {
        error = "The declared PIF region disagrees with the recognized firmware image.";
        return nullptr;
    }
    if (!boot_region)
        boot_region = options.pif_region;
    if (!boot_region) {
        error = "The PIF firmware is not recognized. Declare its region with --pif-region ntsc or pal.";
        return nullptr;
    }
    if (!cart_region && cartridge[0x3e] != 0 && !options.region) {
        error = "The cartridge header region is not recognized. Select --region ntsc or pal explicitly.";
        return nullptr;
    }
    const VideoStandard region = options.region.value_or(cart_region.value_or(*boot_region));
    if (region != *boot_region) {
        error = "The selected console region does not match the PIF firmware. Supply matching firmware or "
                "select the correct --region.";
        return nullptr;
    }
    if (options.ram_mib != 4 && options.ram_mib != 8) {
        error = "Installed RAM must be 4 or 8 MiB.";
        return nullptr;
    }
    const auto save = save_type(options, cartridge, error);
    if (!save)
        return nullptr;

    auto system = std::make_unique<System>(region);
    system->bus.rdram.resize(options.ram_mib * 1024U * 1024U);
    if (!system->bus.load_rom(std::move(cartridge), error))
        return nullptr;
    if (options.cic) {
        system->bus.cic.configure(*options.cic, region == VideoStandard::Pal);
        system->bus.pif[0x7e6] = system->bus.cic.seed();
        system->bus.pif[0x7e7] = system->bus.cic.seed();
    } else if (!system->bus.cic.recognized()) {
        error = "The cartridge boot code is not recognized. Declare its security part with --cic MODEL.";
        return nullptr;
    } else if (!cart_region) {
        system->bus.cic.configure(system->bus.cic.model(), region == VideoStandard::Pal);
    }
    if (system->bus.cic.pal() != (region == VideoStandard::Pal)) {
        error = "The cartridge security part and console have different regions. Check --region, firmware, "
                "and the cartridge's CIC selection.";
        return nullptr;
    }
    system->reset();
    if (!configure_storage(*system, options, *save, error) || !configure_ports(*system, options, error) ||
        !load_persistent_storage(*system, options, error))
        return nullptr;
    if (!system->load_pif(firmware, error) || !system->boot_cartridge(error))
        return nullptr;
    return system;
}

std::string describe_hardware(const System& system) {
    static constexpr std::array cic_names{"6101", "6102", "7102", "6103", "6105", "6106",
                                          "5101", "5167", "8303", "8401", "ddus"};
    static constexpr std::array flash_names{"MX29L0000",  "MX29L0001",  "MX29L1100", "MX29L1101A",
                                            "MX29L1101B", "MX29L1101C", "MN63F81MPN"};
    std::ostringstream text;
    text << "Region=" << (system.video_standard() == VideoStandard::Pal ? "PAL" : "NTSC")
         << " RAM=" << system.bus.rdram.size() / (1024U * 1024U)
         << "MiB CIC=" << cic_names.at(static_cast<std::size_t>(system.bus.cic.model())) << " save=";
    switch (system.bus.save_type) {
    case SaveType::None:
        text << "none";
        break;
    case SaveType::Sram:
        text << "SRAM-" << system.bus.sram.size() / 1024U << "KiB";
        break;
    case SaveType::Eeprom4K:
        text << "EEPROM-4Kbit";
        break;
    case SaveType::Eeprom16K:
        text << "EEPROM-16Kbit";
        break;
    case SaveType::FlashRam:
        text << "FlashRAM-" << flash_names.at(static_cast<std::size_t>(system.bus.flash_chip()));
        break;
    }
    text << " RTC=" << (system.bus.rtc ? "attached" : "none");
    return text.str();
}

} // namespace cupid::host
