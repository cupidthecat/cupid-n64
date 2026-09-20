#pragma once

#include "cartridge/game_boy_fixture.hpp"
#include "controller_pak_fixture.hpp"
#include "pak_crc_oracle.hpp"

namespace test {

struct TransferFixture : PakFixture {
    TransferFixture() {
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState state;
            state.accessory = ControllerAccessory::TransferPak;
            bus.set_controller_state(port, state);
        }
    }

    std::vector<u8> transfer(unsigned port, u16 address, std::span<const u8> data = {}, u8 receive = 0,
                             u8 crc_error = 0) {
        const auto encoded = static_cast<u16>(address | address_remainder(address));
        const bool write = !data.empty();
        std::vector<u8> input{write ? u8{3} : u8{2}, static_cast<u8>(encoded >> 8),
                              static_cast<u8>(encoded ^ crc_error)};
        input.insert(input.end(), data.begin(), data.end());
        if (receive == 0)
            receive = write ? 1 : 33;
        packet(port, input, receive);
        const auto reply = execute(receive);
        CHECK_EQ(bus.pif[response + receive], 0xfeU);
        return reply;
    }

    u8 write_block(unsigned port, u16 address, u8 value, u8 crc_error = 0) {
        std::array<u8, 32> data{};
        data.fill(value);
        const auto reply = transfer(port, address, data, 1, crc_error);
        CHECK_EQ(reply[0], data_remainder(data) ^ (crc_error ? 0xffU : 0U));
        return reply[0];
    }

    void start(unsigned port) {
        status(port);
        write_block(port, 0x8000, 0x84);
        write_block(port, 0xb000, 1);
    }

    std::vector<u8> gb_read(unsigned port, u16 address) {
        write_block(port, 0xa000, static_cast<u8>(address >> 14U));
        return transfer(port, static_cast<u16>(0xc000U | (address & 0x3fffU)));
    }

    void gb_write(unsigned port, u16 address, u8 value) {
        write_block(port, 0xa000, static_cast<u8>(address >> 14U));
        write_block(port, static_cast<u16>(0xc000U | (address & 0x3fffU)), value);
    }
};

} // namespace test
