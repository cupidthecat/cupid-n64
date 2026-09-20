#include "controller_pak_fixture.hpp"
#include "joybus_transport.hpp"
#include "pak_crc_oracle.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;
using test::address_remainder;
using test::data_remainder;

struct RumbleFixture : test::PakFixture {
    RumbleFixture() {
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState state;
            state.accessory = ControllerAccessory::RumblePak;
            bus.set_controller_state(port, state);
        }
    }

    std::vector<u8> transfer(unsigned port, u16 address, bool write, u8 value = 0, u8 receive = 0,
                             unsigned data_length = 32, bool bad_crc = false, u8 flags = 0) {
        const auto encoded = static_cast<u16>(address | address_remainder(address));
        std::vector<u8> input{write ? u8{3} : u8{2}, static_cast<u8>(encoded >> 8),
                              static_cast<u8>(encoded ^ (bad_crc ? 1U : 0U))};
        if (write)
            input.insert(input.end(), data_length, value);
        if (receive == 0)
            receive = write ? 1 : 33;
        packet(port, input, receive, flags);
        const auto reply = execute(receive);
        CHECK_EQ(bus.pif[response + receive], 0xfeU);
        return reply;
    }

    void motor(unsigned port, bool enabled) {
        transfer(port, 0xc000, true, enabled ? 1 : 0);
        CHECK_EQ(bus.rumble_active(port), enabled);
    }
};
} // namespace

TEST(rumble_pak_detection_gates_identification_and_motor_commands_on_all_ports) {
    RumbleFixture fixture;
    const auto storage = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port) {
        const auto blocked = fixture.transfer(port, 0x8000, false);
        for (unsigned index = 0; index < 32; ++index)
            CHECK_EQ(blocked[index], 0U);
        CHECK_EQ(blocked[32], 0xffU);
        std::array<u8, 32> data{};
        data.fill(1);
        CHECK_EQ(fixture.transfer(port, 0xc000, true, 1)[0], data_remainder(data) ^ 0xffU);
        CHECK(!fixture.bus.rumble_active(port));
        CHECK_EQ(fixture.status(port), 3U);
        CHECK_EQ(fixture.status(port), 1U);
        const auto identified = fixture.transfer(port, 0x8000, false);
        data.fill(0x80);
        CHECK(std::equal(data.begin(), data.end(), identified.begin()));
        CHECK_EQ(identified[32], data_remainder(data));
        fixture.motor(port, true);
        CHECK_EQ(fixture.bus.pif[0x7c1 + port], 1U);
    }
    CHECK(fixture.bus.controller_paks == storage);
}

TEST(rumble_pak_every_read_block_decodes_identification_and_motor_state) {
    RumbleFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (bool enabled : {false, true}) {
            fixture.motor(port, enabled);
            for (unsigned block = 0; block < 2048; ++block) {
                const auto address = static_cast<u16>(block * 32);
                std::array<u8, 32> expected{};
                expected.fill(address < 0x8000 ? 0 : address < 0x9000 ? 0x80 : enabled ? 0xff : 0);
                const auto reply = fixture.transfer(port, address, false);
                CHECK(std::equal(expected.begin(), expected.end(), reply.begin()));
                CHECK_EQ(reply[32], data_remainder(expected));
                CHECK_EQ(fixture.bus.pif[0x7c1 + port], 33U);
            }
        }
}

TEST(rumble_pak_every_write_block_uses_first_byte_bit_zero_only_in_motor_range) {
    RumbleFixture fixture;
    fixture.ready();
    const auto storage = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned block = 0; block < 2048; ++block)
            for (u8 value : {u8{0}, u8{1}, u8{0xfe}, u8{0xff}}) {
                const auto address = static_cast<u16>(block * 32);
                const bool requested = (value & 1U) != 0;
                fixture.motor(port, !requested);
                const auto encoded = static_cast<u16>(address | address_remainder(address));
                std::array<u8, 35> input{3, static_cast<u8>(encoded >> 8), static_cast<u8>(encoded)};
                std::fill(input.begin() + 3, input.end(), static_cast<u8>(value ^ 1U));
                input[3] = value;
                fixture.packet(port, input, 1);
                CHECK_EQ(fixture.execute(1)[0],
                         data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
                CHECK_EQ(fixture.bus.rumble_active(port), address >= 0xc000 ? requested : !requested);
            }
    CHECK(fixture.bus.controller_paks == storage);
}

TEST(rumble_pak_invalid_address_crc_never_changes_motor_or_identifies_a_device) {
    RumbleFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (bool enabled : {false, true}) {
            fixture.motor(port, enabled);
            for (u16 address :
                 {u16{0}, u16{0x7fe0}, u16{0x8000}, u16{0x8fe0}, u16{0x9000}, u16{0xc000}, u16{0xffe0}})
                for (unsigned bit = 0; bit < 5; ++bit) {
                    const auto encoded = static_cast<u16>(address | address_remainder(address));
                    std::array<u8, 35> input{3, static_cast<u8>(encoded >> 8),
                                             static_cast<u8>(encoded ^ (1U << bit))};
                    std::fill(input.begin() + 3, input.end(), enabled ? u8{0} : u8{1});
                    fixture.packet(port, input, 1);
                    CHECK_EQ(fixture.execute(1)[0],
                             data_remainder(std::span<const u8, 32>(input.data() + 3, 32)) ^ 0xffU);
                    CHECK_EQ(fixture.bus.rumble_active(port), enabled);
                    input[0] = 2;
                    fixture.packet(port, std::span<const u8>(input).first(3), 33);
                    const auto reply = fixture.execute(33);
                    for (unsigned index = 0; index < 32; ++index)
                        CHECK_EQ(reply[index], 0U);
                    CHECK_EQ(reply[32], 0xffU);
                    CHECK_EQ(fixture.bus.pif[0x7c1 + port], 33U);
                }
        }
}

TEST(rumble_pak_short_and_padded_transfers_keep_motor_effects_and_crc_lengths_separate) {
    RumbleFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port) {
        for (unsigned length = 1; length <= 40; ++length)
            for (bool rejected : {false, true}) {
                fixture.motor(port, false);
                const auto reply = fixture.transfer(port, 0xc000, true, 1, 4, length, rejected);
                std::array<u8, 32> data{};
                data.fill(1);
                const unsigned crc = length < 32 ? 0U : data_remainder(data);
                CHECK_EQ(reply[0], crc ^ (rejected ? 0xffU : 0U));
                CHECK_EQ(reply[1], 0U);
                CHECK_EQ(reply[2], 0U);
                CHECK_EQ(reply[3], 0U);
                CHECK_EQ(fixture.bus.rumble_active(port), !rejected);
            }
        fixture.motor(port, true);
        for (u8 receive = 1; receive <= 40; ++receive) {
            const auto reply = fixture.transfer(port, 0x9000, false, 0, receive);
            std::array<u8, 40> expected{};
            std::fill_n(expected.begin(), 32, u8{0xff});
            expected[32] = 0x0a;
            CHECK(std::equal(reply.begin(), reply.end(), expected.begin()));
        }
    }
}

TEST(rumble_pak_malformed_lengths_preserve_motor_and_set_no_response) {
    RumbleFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port) {
        fixture.motor(port, true);
        for (bool write : {false, true})
            for (u8 send = 1; send <= (write ? 35 : 3); ++send)
                for (u8 receive : {u8{0}, u8{1}}) {
                    if (receive && send >= (write ? 4 : 3))
                        continue;
                    std::array<u8, 35> input{write ? u8{3} : u8{2}, 0xc0, address_remainder(0xc000)};
                    fixture.packet(port, std::span<const u8>(input).first(send), receive);
                    const auto reply = fixture.execute(receive);
                    for (u8 byte : reply)
                        CHECK_EQ(byte, 0xccU);
                    CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive | 0x80U);
                    CHECK(fixture.bus.rumble_active(port));
                }
    }
}

TEST(rumble_pak_status_poll_skips_and_console_reset_preserve_motor_state) {
    RumbleFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port) {
        fixture.motor(port, true);
        for (u8 command : {u8{0}, u8{0xff}, u8{1}, u8{0x7f}}) {
            fixture.command(port, {command}, 3);
            CHECK(fixture.bus.rumble_active(port));
        }
        for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}}) {
            CHECK_EQ(fixture.transfer(port, 0xc000, true, 0, 1, 32, false, flags)[0], 0xccU);
            CHECK(fixture.bus.rumble_active(port));
        }
        auto state = fixture.bus.controllers()[port];
        state.buttons = 0x1030;
        fixture.bus.set_controller_state(port, state);
        fixture.command(port, {1}, 4);
        CHECK(fixture.bus.rumble_active(port));
    }
    fixture.system.reset();
    for (unsigned port = 0; port < 4; ++port) {
        CHECK(fixture.bus.rumble_active(port));
        CHECK_EQ(fixture.status(port), 1U);
    }
}

TEST(rumble_pak_removal_replacement_and_controller_disconnect_stop_only_that_motor) {
    RumbleFixture fixture;
    fixture.ready();
    for (auto& pak : fixture.bus.controller_paks)
        pak.fill(0xa5);
    const auto saved = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        fixture.motor(port, true);
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned action = 0; action < 3; ++action) {
            auto state = fixture.bus.controllers()[port];
            if (action == 0)
                state.accessory = ControllerAccessory::None;
            else if (action == 1)
                state.accessory = ControllerAccessory::ControllerPak;
            else
                state.connected = false;
            fixture.bus.set_controller_state(port, state);
            CHECK(!fixture.bus.rumble_active(port));
            for (unsigned other = 0; other < 4; ++other)
                if (other != port)
                    CHECK(fixture.bus.rumble_active(other));
            if (action < 2)
                CHECK_EQ(fixture.status(port), action == 0 ? 2U : 3U);
            else {
                fixture.command(port, {0}, 3);
                CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x83U);
            }
            state.connected = true;
            state.accessory = ControllerAccessory::RumblePak;
            fixture.bus.set_controller_state(port, state);
            CHECK(!fixture.bus.rumble_active(port));
            CHECK_EQ(fixture.status(port), 3U);
            fixture.motor(port, true);
            CHECK(fixture.bus.controller_paks == saved);
        }
    CHECK(!fixture.bus.rumble_active(4));
}

TEST(rumble_pak_si_dma_initialization_and_motor_readback_obey_completion_boundaries) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read_dma : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true}) {
                    RumbleFixture fixture;
                    test::initialize_memory(fixture.system);
                    u64 fraction = 0;
                    const auto dma = [&](std::span<const u8> input, u8 receive, bool before, bool after) {
                        fixture.packet(port, input, receive);
                        test::configure_joybus(fixture.bus, read_dma);
                        fixture.bus.write(0x04800000, 4, 0x2000);
                        fixture.bus.write(0x04800004, 4, 0x1fc007c0);
                        const u64 rcp = 37020 + port * 1420;
                        const u64 cycles = cpu_clock ? (rcp * 3 - fraction + 1) / 2 : rcp;
                        if (cpu_clock)
                            fraction = (fraction + cycles * 2) % 3;
                        const auto advance = [&](u64 amount) {
                            if (cpu_clock)
                                fixture.system.advance(amount);
                            else
                                fixture.bus.tick(amount);
                        };
                        if (single) {
                            for (u64 index = 0; index < cycles - 1; ++index)
                                advance(1);
                        } else {
                            advance(cycles - 1);
                        }
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 1U);
                        CHECK_EQ(fixture.bus.rumble_active(port), before);
                        advance(1);
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                        CHECK_EQ(fixture.bus.rumble_active(port), after);
                        CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
                        if (read_dma)
                            for (u32 index = 0; index < 64; ++index)
                                CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index),
                                         fixture.bus.pif[0x7c0 + index]);
                        fixture.bus.write(0x04800018, 4, 0);
                    };
                    const std::array<u8, 1> status{0};
                    dma(status, 3, false, false);
                    CHECK_EQ(fixture.bus.pif[fixture.response + 2], 3U);
                    std::array<u8, 35> write{3, 0x80, address_remainder(0x8000)};
                    std::fill(write.begin() + 3, write.end(), u8{0x80});
                    dma(write, 1, false, false);
                    const std::array<u8, 3> identify{2, 0x80, address_remainder(0x8000)};
                    dma(identify, 33, false, false);
                    for (unsigned index = 0; index < 32; ++index)
                        CHECK_EQ(fixture.bus.pif[fixture.response + index], 0x80U);
                    write[1] = 0xc0;
                    write[2] = address_remainder(0xc000);
                    std::fill(write.begin() + 3, write.end(), u8{1});
                    dma(write, 1, false, true);
                    const std::array<u8, 3> readback{2, 0xc0, address_remainder(0xc000)};
                    dma(readback, 33, true, true);
                    for (unsigned index = 0; index < 32; ++index)
                        CHECK_EQ(fixture.bus.pif[fixture.response + index], 0xffU);
                    CHECK_EQ(fixture.bus.pif[fixture.response + 32], 0x0aU);
                    std::fill(write.begin() + 3, write.end(), u8{0});
                    dma(write, 1, true, false);
                }
}
