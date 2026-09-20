#include "cupid/host/media.hpp"

namespace cupid::host {

std::optional<GameBoyCartridgeConfig>
game_boy_configuration(std::span<const u8> cartridge, const TransferOptions& options, std::string& error) {
    error.clear();
    if (cartridge.size() < 0x150) {
        error = "The Game Boy cartridge has no complete header.";
        return std::nullopt;
    }
    GameBoyCartridgeConfig config;
    bool known = true;
    bool has_ram = false;
    switch (cartridge[0x147]) {
    case 0x00:
        break;
    case 0x08:
    case 0x09:
        has_ram = true;
        break;
    case 0x01:
    case 0x02:
    case 0x03:
        config.mapper = GameBoyMapper::Mbc1;
        has_ram = cartridge[0x147] != 0x01;
        break;
    case 0x05:
    case 0x06:
        config.mapper = GameBoyMapper::Mbc2;
        break;
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        config.mapper = GameBoyMapper::Mbc3;
        config.clock = cartridge[0x147] == 0x0f || cartridge[0x147] == 0x10;
        has_ram = cartridge[0x147] == 0x10 || cartridge[0x147] == 0x12 || cartridge[0x147] == 0x13;
        break;
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1d:
    case 0x1e:
        config.mapper = GameBoyMapper::Mbc5;
        config.rumble = cartridge[0x147] >= 0x1c;
        has_ram = cartridge[0x147] != 0x19 && cartridge[0x147] != 0x1c;
        break;
    default:
        known = false;
        break;
    }
    if (!known && !options.mapper) {
        error = "The Game Boy board is not recognized; declare it with --transfer-mapper and --transfer-ram.";
        return std::nullopt;
    }
    if (!known && !options.ram_bytes) {
        error =
            "An unrecognized Game Boy board also requires --transfer-ram, including 0 for no external RAM.";
        return std::nullopt;
    }
    if (has_ram && !options.ram_bytes) {
        switch (cartridge[0x149]) {
        case 1:
            config.ram_bytes = 0x800;
            break;
        case 2:
            config.ram_bytes = 0x2000;
            break;
        case 3:
            config.ram_bytes = 0x8000;
            break;
        case 4:
            config.ram_bytes = 0x20000;
            break;
        case 5:
            config.ram_bytes = 0x10000;
            break;
        default:
            error = "The Game Boy header does not specify a supported RAM capacity; use --transfer-ram.";
            return std::nullopt;
        }
    }
    if (!options.mapper) {
        const unsigned rom_code = cartridge[0x148];
        if (rom_code > 8 || cartridge.size() != (std::size_t{0x8000} << rom_code)) {
            error = "The Game Boy ROM length disagrees with its header; correct the image or declare the "
                    "board explicitly.";
            return std::nullopt;
        }
    }
    config.mapper = options.mapper.value_or(config.mapper);
    config.ram_bytes = options.ram_bytes.value_or(config.ram_bytes);
    config.clock = options.clock.value_or(config.clock);
    config.rumble = options.rumble.value_or(config.rumble);
    return config;
}

} // namespace cupid::host
