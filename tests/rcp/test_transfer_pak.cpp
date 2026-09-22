#include "transfer_pak_fixture.hpp"

namespace {
using namespace cupid;
using test::data_remainder;
using test::TransferFixture;
} // namespace

TEST(transfer_pak_attachment_and_power_gate_identification_and_empty_socket_status) {
    TransferFixture f;
    for (unsigned port = 0; port < 4; ++port) {
        const auto blocked = f.transfer(port, 0x8000);
        CHECK_EQ(blocked[0], 0U);
        CHECK_EQ(blocked[32], 0xffU);
        CHECK_EQ(f.status(port), 3U);
        auto reply = f.transfer(port, 0x8000);
        CHECK_EQ(reply[0], 0U);
        CHECK_EQ(reply[32], 0U);
        f.write_block(port, 0x8000, 0x84);
        reply = f.transfer(port, 0x8000);
        for (unsigned i = 0; i < 32; ++i)
            CHECK_EQ(reply[i], 0x84U);
        CHECK_EQ(reply[32], data_remainder(std::span<const u8, 32>(reply.data(), 32)));
        CHECK_EQ(f.transfer(port, 0xa000)[0], 3U);
        CHECK_EQ(f.transfer(port, 0xb000)[0], 0xc0U);
        f.write_block(port, 0xb000, 1);
        reply = f.transfer(port, 0xb000);
        CHECK_EQ(reply[0], 0xcdU);
        for (unsigned i = 1; i < 32; ++i)
            CHECK_EQ(reply[i], 0xc9U);
        CHECK_EQ(f.transfer(port, 0xc000)[0], 0U);
        f.write_block(port, 0x8000, 0xfe);
        CHECK_EQ(f.transfer(port, 0xb000)[0], 0U);
    }
}

TEST(transfer_pak_status_transitions_advance_per_returned_byte) {
    TransferFixture f;
    f.bus.transfer_paks[0].insert(test::game_boy(GameBoyMapper::Linear));
    f.start(0);
    CHECK_EQ(f.transfer(0, 0xb000, {}, 1)[0], 0x8dU);
    CHECK_EQ(f.transfer(0, 0xb000, {}, 1)[0], 0x89U);
    f.write_block(0, 0xb000, 0);
    auto reply = f.transfer(0, 0xb000);
    CHECK_EQ(reply[0], 0x88U);
    CHECK_EQ(reply[1], 0x84U);
    for (unsigned i = 2; i < 32; ++i)
        CHECK_EQ(reply[i], 0x80U);
    CHECK_EQ(reply[32], data_remainder(std::span<const u8, 32>(reply.data(), 32)));
    f.write_block(0, 0xb000, 1);
    f.write_block(0, 0xb000, 0);
    CHECK_EQ(f.transfer(0, 0xb000)[0], 0x8cU);
}

TEST(transfer_pak_register_mirrors_and_all_bank_values_decode_without_crossing_ports) {
    TransferFixture f;
    f.ready();
    for (unsigned port = 0; port < 4; ++port) {
        f.write_block(port, 0x0000, 0x84);
        for (u16 address : {u16{0}, u16{0x1fe0}, u16{0x8000}, u16{0x9fe0}})
            CHECK_EQ(f.transfer(port, address)[0], 0x84U);
        for (unsigned value = 0; value < 256; ++value) {
            f.write_block(port, 0x2000, static_cast<u8>(value));
            for (u16 address : {u16{0x2000}, u16{0x2fe0}, u16{0xa000}, u16{0xafe0}})
                CHECK_EQ(f.transfer(port, address)[0], value <= 3 ? value : 0U);
        }
        f.write_block(port, 0x8000, 0xfe);
        for (unsigned other = port + 1; other < 4; ++other)
            CHECK_EQ(f.transfer(other, 0x8000)[0], 0U);
    }
}

TEST(transfer_pak_joybus_accesses_real_mapper_registers_and_banked_save_ram) {
    for (GameBoyMapper mapper : {GameBoyMapper::Linear, GameBoyMapper::Mbc1, GameBoyMapper::Mbc2,
                                 GameBoyMapper::Mbc3, GameBoyMapper::Mbc30, GameBoyMapper::Mbc5}) {
        TransferFixture f;
        const unsigned banks = mapper == GameBoyMapper::Linear ? 2U : 8U;
        f.bus.transfer_paks[0].insert(
            test::game_boy(mapper, banks, mapper == GameBoyMapper::Mbc2 ? 0 : 0x2000));
        f.start(0);
        CHECK_EQ(f.gb_read(0, 0x4000)[0], 1U);
        if (mapper != GameBoyMapper::Linear) {
            f.gb_write(0, mapper == GameBoyMapper::Mbc2 ? 0x2100 : 0x2000, 5);
            CHECK_EQ(f.gb_read(0, 0x4000)[0], 5U);
        }
        f.gb_write(0, 0, 10);
        f.gb_write(0, 0xa000, 0x5c);
        const auto reply = f.gb_read(0, 0xa000);
        for (unsigned i = 0; i < 32; ++i)
            CHECK_EQ(reply[i], mapper == GameBoyMapper::Mbc2 ? 0xfcU : 0x5cU);
        CHECK_EQ(reply[32], data_remainder(std::span<const u8, 32>(reply.data(), 32)));
        CHECK_EQ(f.gb_read(0, 0x8000)[0], 0U);
        CHECK_EQ(f.gb_read(0, 0xc000)[0], 0U);
        CHECK_EQ(f.bus.controller_paks[0][0], 0U);
    }
}

TEST(transfer_pak_bad_address_crc_blocks_power_banking_and_cartridge_writes) {
    TransferFixture f;
    f.bus.transfer_paks[0].insert(test::game_boy(GameBoyMapper::Mbc1, 8, 0x2000));
    f.start(0);
    f.gb_write(0, 0, 10);
    f.gb_write(0, 0xa000, 0x77);
    for (u16 address : {u16{0x8000}, u16{0xa000}, u16{0xb000}, u16{0xe000}})
        for (unsigned bit = 0; bit < 5; ++bit) {
            const auto bad = static_cast<u8>(1U << bit);
            f.write_block(0, address, 0xfe, bad);
            const auto reply = f.transfer(0, address, {}, 33, bad);
            for (unsigned i = 0; i < 32; ++i)
                CHECK_EQ(reply[i], 0U);
            CHECK_EQ(reply[32], 0xffU);
            CHECK_EQ(f.transfer(0, 0x8000)[0], 0x84U);
            CHECK_EQ(f.gb_read(0, 0xa000)[0], 0x77U);
        }
}

TEST(transfer_pak_short_and_padded_packets_apply_each_input_byte_once) {
    TransferFixture f;
    f.ready();
    std::array<u8, 1> enable{0x84};
    CHECK_EQ(f.transfer(0, 0x8000, enable)[0], 0U);
    for (unsigned length = 1; length <= 40; ++length) {
        std::vector<u8> data(length);
        for (unsigned i = 0; i < length; ++i)
            data[i] = static_cast<u8>(i & 3U);
        const auto reply = f.transfer(0, 0xa000, data, 4);
        CHECK_EQ(reply[0], length < 32 ? 0U : data_remainder(std::span<const u8, 32>(data.data(), 32)));
        CHECK_EQ(reply[1] | reply[2] | reply[3], 0);
        CHECK_EQ(f.transfer(0, 0xa000)[0], (std::min(length, 32U) - 1U) & 3U);
    }
    for (u8 receive = 1; receive <= 40; ++receive) {
        const auto reply = f.transfer(0, 0x8000, {}, receive);
        std::array<u8, 32> data{};
        data.fill(0x84);
        for (unsigned i = 0; i < receive; ++i)
            CHECK_EQ(reply[i], i < 32 ? 0x84U : i == 32 ? data_remainder(data) : 0U);
        CHECK_EQ(f.bus.pif[0x7c1], receive);
    }
}

TEST(transfer_pak_cartridge_removal_and_controller_disconnect_preserve_only_saved_data) {
    TransferFixture f;
    for (unsigned port = 0; port < 4; ++port) {
        f.bus.transfer_paks[port].insert(test::game_boy(GameBoyMapper::Mbc5, 8, 0x2000, false, true));
        f.start(port);
        f.gb_write(port, 0, 10);
        f.gb_write(port, 0xa000, 0x66);
        f.gb_write(port, 0x2000, 5);
        f.gb_write(port, 0x4000, 8);
        CHECK(f.bus.transfer_paks[port].cartridge()->rumble_active());
        f.system.reset();
        CHECK_EQ(f.gb_read(port, 0x4000)[0], 5U);
        auto state = f.bus.controllers()[port];
        state.connected = false;
        f.bus.set_controller_state(port, state);
        CHECK(!f.bus.transfer_paks[port].cartridge()->rumble_active());
        state.connected = true;
        f.bus.set_controller_state(port, state);
        CHECK_EQ(f.status(port), 3U);
        CHECK_EQ(f.transfer(port, 0x8000)[0], 0U);
        f.start(port);
        CHECK_EQ(f.gb_read(port, 0x4000)[0], 1U);
        CHECK_EQ(f.gb_read(port, 0xa000)[0], 0xffU);
        f.gb_write(port, 0, 10);
        CHECK_EQ(f.gb_read(port, 0xa000)[0], 0x66U);
        f.bus.transfer_paks[port].remove();
        CHECK((f.transfer(port, 0xb000)[0] & 0x40U) != 0);
        CHECK_EQ(f.gb_read(port, 0)[0], 0U);
    }
}

TEST(transfer_pak_battery_clock_advances_while_access_is_disabled) {
    TransferFixture f;
    f.bus.transfer_paks[0].insert(test::game_boy(GameBoyMapper::Mbc3, 2, 0, true));
    f.bus.tick(GameBoyCartridge::cycles_per_second);
    f.start(0);
    f.gb_write(0, 0, 10);
    f.gb_write(0, 0x6000, 0);
    f.gb_write(0, 0x6000, 1);
    f.gb_write(0, 0x4000, 8);
    CHECK_EQ(f.gb_read(0, 0xa000)[0], 1U);
    f.write_block(0, 0x8000, 0xfe);
    f.bus.tick(GameBoyCartridge::cycles_per_second);
    f.start(0);
    f.gb_write(0, 0, 10);
    f.gb_write(0, 0x6000, 0);
    f.gb_write(0, 0x6000, 1);
    f.gb_write(0, 0x4000, 8);
    CHECK_EQ(f.gb_read(0, 0xa000)[0], 2U);
}
