#pragma once

#include "cupid/cartridge/game_boy.hpp"
#include "test.hpp"

#include <algorithm>
#include <utility>

namespace test {
using namespace cupid;

inline std::vector<u8> game_boy_rom(unsigned banks) {
    std::vector<u8> rom(banks * 0x4000U);
    for (unsigned bank = 0; bank < banks; ++bank) {
        std::fill_n(rom.begin() + bank * 0x4000U, 0x4000, static_cast<u8>(bank));
        rom[bank * 0x4000U + 1] = static_cast<u8>(bank >> 8U);
    }
    return rom;
}

inline GameBoyCartridge game_boy(GameBoyMapper mapper, unsigned banks = 2, unsigned ram = 0,
                                 bool clock = false, bool rumble = false) {
    std::string error;
    auto result = GameBoyCartridge::create(game_boy_rom(banks), {mapper, ram, clock, rumble}, error);
    CHECK(result.has_value());
    CHECK(error.empty());
    return std::move(*result);
}

inline unsigned game_boy_bank(const GameBoyCartridge& cart, u16 address = 0x4000) {
    return cart.read(address) | (static_cast<unsigned>(cart.read(static_cast<u16>(address + 1))) << 8U);
}

inline u8 game_boy_rtc_read(GameBoyCartridge& cart, u8 bank) {
    cart.write(0x4000, bank);
    return cart.read(0xa000);
}

inline void game_boy_rtc_latch(GameBoyCartridge& cart) {
    cart.write(0x6000, 0);
    cart.write(0x6000, 1);
}

} // namespace test
