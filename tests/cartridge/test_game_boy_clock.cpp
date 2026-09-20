#include "game_boy_fixture.hpp"

#include <limits>

namespace {
using namespace cupid;
using test::game_boy;
using test::game_boy_rtc_latch;
using test::game_boy_rtc_read;
constexpr u64 second = GameBoyCartridge::cycles_per_second;
} // namespace

TEST(game_boy_clock_latches_hold_while_the_live_counter_advances) {
    auto cart = game_boy(GameBoyMapper::Mbc3, 2, 0, true);
    cart.set_clock({58, 59, 23, 511});
    cart.write(0, 10);
    game_boy_rtc_latch(cart);
    cart.tick(second - 1);
    CHECK_EQ(cart.clock().seconds, 58U);
    cart.tick(1);
    CHECK_EQ(cart.clock().seconds, 59U);
    CHECK_EQ(game_boy_rtc_read(cart, 8), 58U);
    cart.tick(second);
    CHECK_EQ(cart.clock().seconds, 0U);
    CHECK_EQ(cart.clock().minutes, 0U);
    CHECK_EQ(cart.clock().hours, 0U);
    CHECK_EQ(cart.clock().days, 0U);
    CHECK(cart.clock().carry);
    CHECK_EQ(game_boy_rtc_read(cart, 12), 1U);
    cart.write(0x6000, 1);
    CHECK_EQ(game_boy_rtc_read(cart, 8), 58U);
    game_boy_rtc_latch(cart);
    CHECK_EQ(game_boy_rtc_read(cart, 8), 0U);
    CHECK_EQ(game_boy_rtc_read(cart, 12), 0x80U);
}

TEST(game_boy_clock_register_masks_halt_and_latch_survive_mapper_power) {
    auto cart = game_boy(GameBoyMapper::Mbc30, 2, 0, true);
    cart.write(0, 10);
    for (u8 bank = 8; bank <= 12; ++bank) {
        cart.write(0x4000, bank);
        cart.write(0xbfff, 0xff);
    }
    game_boy_rtc_latch(cart);
    CHECK_EQ(game_boy_rtc_read(cart, 8), 63U);
    CHECK_EQ(game_boy_rtc_read(cart, 9), 63U);
    CHECK_EQ(game_boy_rtc_read(cart, 10), 31U);
    CHECK_EQ(game_boy_rtc_read(cart, 11), 255U);
    CHECK_EQ(game_boy_rtc_read(cart, 12), 0xc1U);
    cart.tick(second * 86400);
    CHECK_EQ(cart.clock().seconds, 63U);
    cart.power();
    CHECK_EQ(cart.read(0xa000), 0xffU);
    cart.write(0, 10);
    CHECK_EQ(game_boy_rtc_read(cart, 12), 0xc1U);
    cart.write(0xa000, 1);
    CHECK(cart.clock_running());
    CHECK(!cart.clock().carry);
    CHECK_EQ(game_boy_rtc_read(cart, 12), 0xc1U);
    cart.tick(second);
    CHECK_EQ(cart.clock().seconds, 0U);
    CHECK_EQ(cart.clock().minutes, 63U);
    cart.tick(second * 60);
    CHECK_EQ(cart.clock().minutes, 0U);
    CHECK_EQ(cart.clock().hours, 31U);
    cart.tick(second * 3600);
    CHECK_EQ(cart.clock().hours, 0U);
    CHECK_EQ(cart.clock().days, 511U);
}

TEST(game_boy_clock_ram_enable_gates_registers_without_stopping_the_clock) {
    auto cart = game_boy(GameBoyMapper::Mbc3, 2, 0, true);
    cart.set_clock({12});
    cart.write(0x4000, 8);
    cart.write(0xa000, 55);
    CHECK_EQ(cart.read(0xa000), 0xffU);
    cart.tick(second);
    cart.write(0, 0xfa);
    game_boy_rtc_latch(cart);
    CHECK_EQ(cart.read(0xa000), 13U);
    cart.write(0, 0);
    cart.write(0xa000, 33);
    cart.write(0, 10);
    game_boy_rtc_latch(cart);
    CHECK_EQ(cart.read(0xa000), 13U);
    auto no_oscillator = game_boy(GameBoyMapper::Mbc3);
    no_oscillator.write(0, 10);
    no_oscillator.write(0x4000, 8);
    no_oscillator.write(0xa000, 17);
    no_oscillator.tick(second * 100);
    game_boy_rtc_latch(no_oscillator);
    CHECK_EQ(no_oscillator.read(0xa000), 17U);
}

TEST(game_boy_clock_bulk_advances_match_single_cycles_and_handle_large_intervals) {
    auto bulk = game_boy(GameBoyMapper::Mbc3, 2, 0, true);
    auto single = bulk;
    bulk.tick(second - 100);
    single.tick(second - 100);
    bulk.tick(200);
    for (unsigned i = 0; i < 200; ++i)
        single.tick(1);
    CHECK_EQ(bulk.clock().seconds, 1U);
    CHECK_EQ(single.clock().seconds, 1U);
    CHECK_EQ(bulk.next_tick(), single.next_tick());
    bulk.set_clock({});
    bulk.tick(std::numeric_limits<u64>::max());
    CHECK_EQ(bulk.clock().seconds, 59U);
    CHECK_EQ(bulk.clock().minutes, 12U);
    CHECK_EQ(bulk.clock().hours, 17U);
    CHECK_EQ(bulk.clock().days, 511U);
    CHECK(bulk.clock().carry);
}
