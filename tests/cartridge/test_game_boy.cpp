#include "game_boy_fixture.hpp"

namespace {
using namespace cupid;
using test::game_boy;
using test::game_boy_bank;
} // namespace

TEST(game_boy_configuration_rejects_unsupported_sizes_and_peripheral_combinations) {
    std::string error;
    for (unsigned size : {0U, 0x150U, 0x4000U, 0x8001U, 0x10000U}) {
        CHECK(!GameBoyCartridge::create(std::vector<u8>(size), {}, error));
        CHECK(!error.empty());
    }
    for (GameBoyCartridgeConfig config : {GameBoyCartridgeConfig{GameBoyMapper::Linear, 0x8000},
                                          GameBoyCartridgeConfig{GameBoyMapper::Mbc2, 256},
                                          GameBoyCartridgeConfig{GameBoyMapper::Mbc3, 0x10000},
                                          GameBoyCartridgeConfig{GameBoyMapper::Mbc5, 0x20000, false, true},
                                          GameBoyCartridgeConfig{GameBoyMapper::Mbc1, 0, true},
                                          GameBoyCartridgeConfig{GameBoyMapper::Mbc3, 0, false, true},
                                          GameBoyCartridgeConfig{static_cast<GameBoyMapper>(99)},
                                          GameBoyCartridgeConfig{GameBoyMapper::Mbc5, 0x3000}}) {
        CHECK(!GameBoyCartridge::create(test::game_boy_rom(2), config, error));
        CHECK(!error.empty());
    }
    CHECK(!GameBoyCartridge::create(test::game_boy_rom(64), {GameBoyMapper::Mbc1, 0x8000}, error));
    CHECK(GameBoyCartridge::create(test::game_boy_rom(2), {}, error).has_value());
    CHECK(error.empty());
}

TEST(game_boy_linear_rom_ram_and_unmapped_addresses_are_separate) {
    auto cart = game_boy(GameBoyMapper::Linear, 2, 0x800);
    CHECK_EQ(game_boy_bank(cart, 0), 0U);
    CHECK_EQ(game_boy_bank(cart), 1U);
    for (unsigned address = 0; address < 0x8000; ++address)
        cart.write(static_cast<u16>(address), 0xff);
    CHECK_EQ(game_boy_bank(cart), 1U);
    for (unsigned offset = 0; offset < 0x800; ++offset)
        cart.write(static_cast<u16>(0xa000 + offset), static_cast<u8>(offset * 7U));
    for (unsigned offset = 0; offset < 0x2000; ++offset)
        CHECK_EQ(cart.read(static_cast<u16>(0xa000 + offset)), static_cast<u8>(offset * 7U));
    for (u16 address : {u16{0x8000}, u16{0x9fff}, u16{0xc000}, u16{0xff50}, u16{0xffff}}) {
        cart.write(address, 0xff);
        CHECK_EQ(cart.read(address), 0U);
    }
    CHECK_EQ(game_boy(GameBoyMapper::Linear).read(0xa000), 0U);
    CHECK_EQ(game_boy(GameBoyMapper::Mbc1).read(0xa000), 0xffU);
}

TEST(game_boy_mbc1_mode_and_bank_registers_cover_both_rom_windows) {
    auto cart = game_boy(GameBoyMapper::Mbc1, 128, 0x2000);
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned high = 0; high < 4; ++high)
            for (unsigned low = 0; low < 256; ++low) {
                cart.write(0x6000, static_cast<u8>(mode));
                cart.write(0x4000, static_cast<u8>(high | 0xfcU));
                cart.write(0x2000, static_cast<u8>(low));
                CHECK_EQ(game_boy_bank(cart, 0), mode ? high * 32U : 0U);
                CHECK_EQ(game_boy_bank(cart), high * 32U + ((low & 31U) ? low & 31U : 1U));
            }
    auto small = game_boy(GameBoyMapper::Mbc1, 16);
    small.write(0x2000, 16);
    CHECK_EQ(game_boy_bank(small), 0U);
    small.write(0x2000, 0);
    CHECK_EQ(game_boy_bank(small), 1U);
}

TEST(game_boy_mbc1_ram_enable_and_mode_select_four_independent_banks) {
    auto cart = game_boy(GameBoyMapper::Mbc1, 32, 0x8000);
    for (unsigned value = 0; value < 256; ++value) {
        cart.write(0, static_cast<u8>(value));
        CHECK_EQ(cart.read(0xa000), (value & 15U) == 10U ? 0U : 0xffU);
    }
    cart.write(0, 0x1a);
    cart.write(0x6000, 1);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cart.write(0x4000, static_cast<u8>(bank));
        cart.write(0xa000, static_cast<u8>(0x40 + bank));
        cart.write(0xbfff, static_cast<u8>(0x80 + bank));
    }
    for (unsigned bank = 0; bank < 4; ++bank) {
        cart.write(0x4000, static_cast<u8>(bank));
        CHECK_EQ(cart.read(0xa000), 0x40U + bank);
        CHECK_EQ(cart.read(0xbfff), 0x80U + bank);
    }
    cart.write(0x6000, 0);
    CHECK_EQ(cart.read(0xa000), 0x40U);
    cart.write(0, 0);
    cart.write(0xa000, 0xee);
    cart.write(0, 10);
    CHECK_EQ(cart.read(0xa000), 0x40U);
}

TEST(game_boy_mbc2_address_bit_eight_selects_banking_or_ram_enable) {
    auto cart = game_boy(GameBoyMapper::Mbc2, 16);
    CHECK_EQ(cart.ram().size(), 256U);
    for (unsigned value = 0; value < 256; ++value) {
        cart.write(0x0100, static_cast<u8>(value));
        CHECK_EQ(game_boy_bank(cart), (value & 15U) ? value & 15U : 1U);
        cart.write(0x2000, static_cast<u8>(value));
        CHECK_EQ(cart.read(0xa000), (value & 15U) == 10U ? 0xf0U : 0xffU);
        CHECK_EQ(game_boy_bank(cart), (value & 15U) ? value & 15U : 1U);
    }
    cart.write(0, 10);
    for (unsigned offset = 0; offset < 512; ++offset)
        cart.write(static_cast<u16>(0xa000 + offset), static_cast<u8>(offset * 3U));
    for (unsigned offset = 0; offset < 0x2000; ++offset)
        CHECK_EQ(cart.read(static_cast<u16>(0xa000 + offset)), 0xf0U | ((offset * 3U) & 15U));
    CHECK_EQ(cart.ram()[0], 0x30U);
    cart.write(0, 0);
    cart.write(0xa001, 0);
    cart.write(0, 10);
    CHECK_EQ(cart.read(0xa001), 0xf3U);
}

TEST(game_boy_mbc3_and_mbc30_address_all_rom_and_ram_banks) {
    for (bool extended : {false, true}) {
        const unsigned banks = extended ? 256U : 128U;
        const unsigned ram_banks = extended ? 8U : 4U;
        auto cart =
            game_boy(extended ? GameBoyMapper::Mbc30 : GameBoyMapper::Mbc3, banks, ram_banks * 0x2000);
        for (unsigned value = 0; value < 256; ++value) {
            cart.write(0x2000, static_cast<u8>(value));
            const unsigned bank = value % banks;
            CHECK_EQ(game_boy_bank(cart), bank == 0 ? 1 : bank);
            CHECK_EQ(game_boy_bank(cart, 0), 0U);
        }
        cart.write(0, 10);
        for (unsigned bank = 0; bank < ram_banks; ++bank) {
            cart.write(0x4000, static_cast<u8>(bank));
            cart.write(0xa000, static_cast<u8>(0x90 + bank));
        }
        for (unsigned bank = 0; bank < ram_banks; ++bank) {
            cart.write(0x4000, static_cast<u8>(bank));
            CHECK_EQ(cart.read(0xa000), 0x90U + bank);
        }
        for (u8 bank : {u8{13}, u8{14}, u8{15}}) {
            cart.write(0x4000, bank);
            cart.write(0xa000, 0x55);
            CHECK_EQ(cart.read(0xa000), 0xffU);
        }
    }
}

TEST(game_boy_mbc5_nine_bit_rom_bank_and_rumble_ram_wiring_are_independent) {
    for (bool rumble : {false, true}) {
        const unsigned ram_banks = rumble ? 8U : 16U;
        auto cart = game_boy(GameBoyMapper::Mbc5, 512, ram_banks * 0x2000, false, rumble);
        for (unsigned bank = 0; bank < 512; ++bank) {
            cart.write(0x2000, static_cast<u8>(bank));
            cart.write(0x3000, static_cast<u8>(bank >> 8U));
            CHECK_EQ(game_boy_bank(cart), bank);
            CHECK_EQ(game_boy_bank(cart, 0), 0U);
        }
        cart.write(0, 10);
        for (unsigned bank = 0; bank < ram_banks; ++bank) {
            cart.write(0x4000, static_cast<u8>(bank));
            cart.write(0xa000, static_cast<u8>(bank + 1));
        }
        for (unsigned value = 0; value < 256; ++value) {
            cart.write(0x4000, static_cast<u8>(value));
            CHECK_EQ(cart.read(0xa000), value % ram_banks + 1U);
            CHECK_EQ(cart.rumble_active(), rumble && (value & 8U) != 0);
        }
        cart.power();
        CHECK_EQ(game_boy_bank(cart), 1U);
        CHECK_EQ(cart.read(0xa000), 0xffU);
        CHECK(!cart.rumble_active());
        cart.write(0, 10);
        CHECK_EQ(cart.read(0xa000), 1U);
    }
}
