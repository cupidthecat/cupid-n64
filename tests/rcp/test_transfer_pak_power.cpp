#include "transfer_pak_fixture.hpp"

namespace {
using namespace cupid;
using test::TransferFixture;
} // namespace

TEST(transfer_pak_disable_preserves_mapper_until_cartridge_access_restarts) {
    for (GameBoyMapper mapper : {GameBoyMapper::Mbc1, GameBoyMapper::Mbc2, GameBoyMapper::Mbc3,
                                 GameBoyMapper::Mbc30, GameBoyMapper::Mbc5}) {
        TransferFixture fixture;
        fixture.bus.transfer_paks[0].insert(
            test::game_boy(mapper, 8, mapper == GameBoyMapper::Mbc2 ? 0 : 0x2000));
        fixture.start(0);
        fixture.gb_write(0, 0, 10);
        fixture.gb_write(0, 0xa000, 6);
        fixture.gb_write(0, mapper == GameBoyMapper::Mbc2 ? 0x2100 : 0x2000, 5);
        auto& cartridge = *fixture.bus.transfer_paks[0].cartridge();
        const u8 saved = mapper == GameBoyMapper::Mbc2 ? 0xf6 : 6;
        CHECK_EQ(test::game_boy_bank(cartridge), 5U);
        CHECK_EQ(cartridge.read(0xa000), saved);

        // Each byte in the Joybus block repeats the disable command.
        fixture.write_block(0, 0x8000, 0xfe);
        CHECK_EQ(fixture.transfer(0, 0xc000)[0], 0U);
        CHECK_EQ(test::game_boy_bank(cartridge), 5U);
        CHECK_EQ(cartridge.read(0xa000), saved);

        fixture.write_block(0, 0xb000, 1);
        CHECK_EQ(test::game_boy_bank(cartridge), 5U);
        fixture.write_block(0, 0x8000, 0x84);
        CHECK_EQ(fixture.transfer(0, 0xa000)[0], 3U);
        CHECK_EQ(fixture.transfer(0, 0xb000)[0], 0x80U);
        CHECK_EQ(fixture.transfer(0, 0xc000)[0], 0U);
        CHECK_EQ(test::game_boy_bank(cartridge), 5U);
        CHECK_EQ(cartridge.read(0xa000), saved);

        fixture.write_block(0, 0xb000, 1);
        CHECK_EQ(fixture.transfer(0, 0xb000, {}, 1)[0], 0x8dU);
        CHECK_EQ(test::game_boy_bank(cartridge), 1U);
        CHECK_EQ(cartridge.read(0xa000), 0xffU);
        fixture.gb_write(0, 0, 10);
        CHECK_EQ(fixture.gb_read(0, 0xa000)[0], saved);
    }
}
