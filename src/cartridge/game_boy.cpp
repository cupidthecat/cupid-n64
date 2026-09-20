#include "cupid/cartridge/game_boy.hpp"

#include <bit>
#include <utility>

namespace cupid {

std::optional<GameBoyCartridge> GameBoyCartridge::create(std::vector<u8> rom, GameBoyCartridgeConfig config,
                                                         std::string& error) {
    unsigned max_rom = 0, max_ram = 0;
    switch (config.mapper) {
    case GameBoyMapper::Linear:
        max_rom = 0x8000;
        max_ram = 0x2000;
        break;
    case GameBoyMapper::Mbc1:
        max_rom = 0x200000;
        max_ram = rom.size() > 0x80000 ? 0x2000 : 0x8000;
        break;
    case GameBoyMapper::Mbc2:
        max_rom = 0x40000;
        break;
    case GameBoyMapper::Mbc3:
        max_rom = 0x200000;
        max_ram = 0x8000;
        break;
    case GameBoyMapper::Mbc30:
        max_rom = 0x400000;
        max_ram = 0x10000;
        break;
    case GameBoyMapper::Mbc5:
        max_rom = 0x800000;
        max_ram = config.rumble ? 0x10000 : 0x20000;
        break;
    }
    if (rom.size() < 0x8000 || rom.size() > max_rom || !std::has_single_bit(rom.size())) {
        error = "Game Boy ROM size must be a supported power of two within the selected mapper's capacity";
        return std::nullopt;
    }
    if (config.ram_bytes > max_ram ||
        (config.ram_bytes != 0 && (config.ram_bytes < 0x800 || !std::has_single_bit(config.ram_bytes)))) {
        error = "Game Boy RAM size is not supported by the selected mapper; MBC2 supplies its own nibble RAM";
        return std::nullopt;
    }
    if ((config.clock && config.mapper != GameBoyMapper::Mbc3 && config.mapper != GameBoyMapper::Mbc30) ||
        (config.rumble && config.mapper != GameBoyMapper::Mbc5)) {
        error = "Game Boy clock requires MBC3/MBC30 and cartridge rumble requires MBC5";
        return std::nullopt;
    }
    error.clear();
    return GameBoyCartridge(std::move(rom), config);
}

GameBoyCartridge::GameBoyCartridge(std::vector<u8> rom, GameBoyCartridgeConfig config)
    : config_(config), rom_(std::move(rom)),
      ram_(config.mapper == GameBoyMapper::Mbc2 ? 256 : config.ram_bytes) {}

bool GameBoyCartridge::mbc3() const {
    return config_.mapper == GameBoyMapper::Mbc3 || config_.mapper == GameBoyMapper::Mbc30;
}

void GameBoyCartridge::power() {
    rom_bank_ = 1;
    ram_bank_ = 0;
    ram_enabled_ = false;
    banking_mode_ = false;
    rumble_active_ = false;
}

unsigned GameBoyCartridge::ram_offset(u16 address) const {
    const unsigned bank =
        config_.mapper == GameBoyMapper::Linear || (config_.mapper == GameBoyMapper::Mbc1 && !banking_mode_)
            ? 0
            : ram_bank_;
    return ((bank << 13U) | (address & 0x1fffU)) & static_cast<unsigned>(ram_.size() - 1U);
}

u8 GameBoyCartridge::read(u16 address) const {
    if (address < 0x8000) {
        unsigned bank = address < 0x4000 ? 0 : rom_bank_;
        if (config_.mapper == GameBoyMapper::Linear)
            bank = address >> 14U;
        else if (config_.mapper == GameBoyMapper::Mbc1 && (address >= 0x4000 || banking_mode_))
            bank |= ram_bank_ << 5U;
        return rom_[((bank << 14U) | (address & 0x3fffU)) & (rom_.size() - 1U)];
    }
    if (address < 0xa000 || address >= 0xc000)
        return 0;
    if (config_.mapper != GameBoyMapper::Linear && !ram_enabled_)
        return 0xff;
    if (mbc3() && ram_bank_ > (config_.mapper == GameBoyMapper::Mbc30 ? 7U : 3U))
        return ram_bank_ >= 8 && ram_bank_ <= 12 ? read_clock() : 0xff;
    if (ram_.empty())
        return config_.mapper == GameBoyMapper::Linear ? 0 : 0xff;
    if (config_.mapper == GameBoyMapper::Mbc2)
        return static_cast<u8>(0xf0U | ((ram_[(address & 0x1ffU) >> 1U] >> ((address & 1U) * 4U)) & 15U));
    return ram_[ram_offset(address)];
}

void GameBoyCartridge::write(u16 address, u8 value) {
    if (address >= 0xa000 && address < 0xc000) {
        if (config_.mapper != GameBoyMapper::Linear && !ram_enabled_)
            return;
        if (mbc3() && ram_bank_ > (config_.mapper == GameBoyMapper::Mbc30 ? 7U : 3U)) {
            if (ram_bank_ >= 8 && ram_bank_ <= 12)
                write_clock(value);
        } else if (!ram_.empty()) {
            if (config_.mapper == GameBoyMapper::Mbc2) {
                auto& pair = ram_[(address & 0x1ffU) >> 1U];
                const unsigned shift = (address & 1U) * 4U;
                pair = static_cast<u8>((pair & ~(15U << shift)) | ((value & 15U) << shift));
            } else {
                ram_[ram_offset(address)] = value;
            }
        }
        return;
    }
    if (address >= 0x8000 || config_.mapper == GameBoyMapper::Linear)
        return;
    if (config_.mapper == GameBoyMapper::Mbc2) {
        if (address < 0x4000) {
            if ((address & 0x100U) == 0)
                ram_enabled_ = (value & 15U) == 10U;
            else
                rom_bank_ = (value & 15U) == 0 ? 1 : value & 15U;
        }
    } else if (address < 0x2000) {
        ram_enabled_ = (value & 15U) == 10U;
    } else if (address < 0x4000) {
        if (config_.mapper == GameBoyMapper::Mbc5) {
            rom_bank_ =
                address < 0x3000 ? (rom_bank_ & 0x100U) | value : (rom_bank_ & 0xffU) | ((value & 1U) << 8U);
        } else {
            const unsigned mask = config_.mapper == GameBoyMapper::Mbc1   ? 31U
                                  : config_.mapper == GameBoyMapper::Mbc3 ? 127U
                                                                          : 255U;
            rom_bank_ = (value & mask) == 0 ? 1 : value & mask;
        }
    } else if (address < 0x6000) {
        if (config_.mapper == GameBoyMapper::Mbc1)
            ram_bank_ = value & 3U;
        else if (config_.mapper == GameBoyMapper::Mbc5) {
            ram_bank_ = value & (config_.rumble ? 7U : 15U);
            rumble_active_ = config_.rumble && (value & 8U) != 0;
        } else {
            ram_bank_ = value & 15U;
        }
    } else if (config_.mapper == GameBoyMapper::Mbc1) {
        banking_mode_ = (value & 1U) != 0;
    } else if (mbc3()) {
        if (latch_value_ == 0 && value == 1)
            latched_ = clock_;
        latch_value_ = value & 1U;
    }
}

} // namespace cupid
