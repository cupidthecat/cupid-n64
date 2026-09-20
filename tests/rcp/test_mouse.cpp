#include "controller_pak_fixture.hpp"
#include "joybus_transport.hpp"
#include "pak_crc_oracle.hpp"
#include "test_system.hpp"

#include <limits>

namespace {
using namespace cupid;

struct MouseFixture : test::PakFixture {
    MouseFixture() {
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState state;
            state.device = ControllerDevice::Mouse;
            bus.set_controller_state(port, state);
        }
    }

    std::vector<u8> poll(unsigned port, u8 receive = 4) {
        return command(port, {1}, receive);
    }
};
} // namespace

TEST(mouse_identification_uses_its_own_device_type_for_every_accessory_selection) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (auto accessory : {ControllerAccessory::None, ControllerAccessory::ControllerPak,
                               ControllerAccessory::RumblePak}) {
            auto state = fixture.bus.controllers()[port];
            state.accessory = accessory;
            fixture.bus.set_controller_state(port, state);
            for (u8 command : {u8{0}, u8{0xff}})
                for (u8 receive = 0; receive <= 8; ++receive) {
                    const auto reply = fixture.command(port, {command, 0x55}, receive);
                    const std::array<u8, 8> expected{2, 0, 2};
                    CHECK(std::equal(reply.begin(), reply.end(), expected.begin()));
                    CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
                    CHECK_EQ(fixture.bus.pif[fixture.response + receive], 0xfeU);
                }
        }
}

TEST(mouse_buttons_are_levels_and_motion_is_consumed_once_per_poll) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned buttons = 0; buttons < 4; ++buttons) {
            auto state = fixture.bus.controllers()[port];
            state.buttons = 0xffff;
            state.stick_x = -128;
            state.stick_y = 127;
            fixture.bus.set_controller_state(port, state);
            fixture.bus.add_mouse_input(port, {(buttons & 1) != 0, (buttons & 2) != 0, -17, 43});
            const u8 expected = static_cast<u8>(((buttons & 1) ? 0x80 : 0) | ((buttons & 2) ? 0x40 : 0));
            CHECK_EQ(fixture.poll(port), (std::vector<u8>{expected, 0, 0xef, 0xd5}));
            CHECK_EQ(fixture.poll(port), (std::vector<u8>{expected, 0, 0, 0}));
            CHECK_EQ(fixture.bus.controllers()[port].buttons, 0xffffU);
        }
}

TEST(mouse_axes_saturate_after_direction_conversion_and_discard_excess_motion) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (s32 motion = -32768; motion <= 32767; ++motion) {
            fixture.bus.add_mouse_input(port, {false, false, motion, motion});
            const auto reply = fixture.poll(port);
            const s32 x = motion < -128 ? -128 : motion > 127 ? 127 : motion;
            const s32 y = motion < -127 ? 127 : motion > 128 ? -128 : -motion;
            CHECK_EQ(reply[2], static_cast<u8>(x));
            CHECK_EQ(reply[3], static_cast<u8>(y));
            CHECK_EQ(fixture.poll(port), (std::vector<u8>{0, 0, 0, 0}));
        }
}

TEST(mouse_input_updates_accumulate_motion_and_replace_button_levels_without_overflow) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port) {
        fixture.bus.add_mouse_input(port, {true, false, 1000, -1000});
        fixture.bus.add_mouse_input(port, {false, true, -990, 985});
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0x40, 0, 10, 15}));
        fixture.bus.add_mouse_input(
            port, {true, true, std::numeric_limits<s32>::max(), std::numeric_limits<s32>::min()});
        fixture.bus.add_mouse_input(port, {false, false, 1, -1});
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0, 0, 127, 127}));
        fixture.bus.add_mouse_input(
            port, {true, true, std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max()});
        fixture.bus.add_mouse_input(port, {true, false, -1, 1});
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0x80, 0, 0x80, 0x80}));
    }
}

TEST(mouse_poll_prefixes_consume_both_axes_and_long_replies_report_overflow) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (u8 receive = 0; receive <= 8; ++receive) {
            fixture.bus.add_mouse_input(port, {true, false, 17, 43});
            const auto reply = fixture.command(port, {1, 0xaa}, receive);
            const std::array<u8, 8> expected{0x80, 0, 17, 0xd5};
            CHECK(std::equal(reply.begin(), reply.end(), expected.begin()));
            CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive | (receive > 4 ? 0x40U : 0U));
            CHECK_EQ(fixture.bus.pif[fixture.response + receive], 0xfeU);
            CHECK_EQ(fixture.poll(port), (std::vector<u8>{0x80, 0, 0, 0}));
        }
}

TEST(mouse_unsupported_commands_preserve_motion_reply_bytes_and_pak_storage) {
    MouseFixture fixture;
    for (auto& pak : fixture.bus.controller_paks)
        pak.fill(0x3c);
    const auto saved = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned command = 2; command < 255; ++command) {
            fixture.bus.add_mouse_input(port, {false, true, 4, -9});
            std::array<u8, 35> input{};
            input[0] = static_cast<u8>(command);
            input[1] = 0xc0;
            input[2] = test::address_remainder(0xc000);
            std::fill(input.begin() + 3, input.end(), u8{1});
            fixture.packet(port, input, 4);
            CHECK_EQ(fixture.execute(4), (std::vector<u8>{0xcc, 0xcc, 0xcc, 0xcc}));
            CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x84U);
            CHECK_EQ(fixture.poll(port), (std::vector<u8>{0x40, 0, 4, 9}));
            CHECK(!fixture.bus.rumble_active(port));
        }
    CHECK_EQ(fixture.bus.controller_paks, saved);
}

TEST(mouse_status_skip_and_console_reset_preserve_pending_input) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port) {
        fixture.bus.add_mouse_input(port, {true, true, 7, -8});
        fixture.command(port, {0}, 3);
        fixture.command(port, {0xff}, 0);
        fixture.command(port, {1}, 4, 0x80);
        fixture.command(port, {1}, 4, 0x40);
        fixture.packet(port, {}, 0);
        fixture.bus.pif[0x7c0 + port] = 0xfd;
        fixture.execute(0);
        fixture.system.reset();
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0xc0, 0, 7, 8}));
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0xc0, 0, 0, 0}));
    }
}

TEST(mouse_motion_survives_ordinary_state_updates_in_a_mixed_device_packet) {
    MouseFixture fixture;
    fixture.bus.set_controller_state(0, ControllerState{});
    auto state = fixture.bus.controllers()[2];
    state.connected = false;
    fixture.bus.set_controller_state(2, state);
    state = ControllerState{};
    state.buttons = 0x8000;
    state.stick_x = 17;
    fixture.bus.set_controller_state(3, state);
    fixture.bus.rtc.emplace(CartridgeRtc::Registers{});
    fixture.bus.add_mouse_input(1, {true, false, 23, -34});
    state = fixture.bus.controllers()[1];
    state.buttons = 0xffff;
    state.accessory = ControllerAccessory::RumblePak;
    fixture.bus.set_controller_state(1, state);
    const std::array<u8, 33> packet{1,    3,    0,    0xcc, 0xcc, 0xcc, 1,    4,    1,    0xcc, 0xcc,
                                    0xcc, 0xcc, 1,    3,    0,    0xcc, 0xcc, 0xcc, 1,    4,    1,
                                    0xcc, 0xcc, 0xcc, 0xcc, 1,    3,    6,    0xcc, 0xcc, 0xcc, 0xfe};
    std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
    std::copy(packet.begin(), packet.end(), fixture.bus.pif.begin() + 0x7c0);
    fixture.bus.joybus.configure();
    fixture.bus.joybus.execute();
    const std::array<u8, 33> expected{1,    3,  0,  5,    0, 3,    1,    4,    1,    0x80, 0,
                                      23,   34, 1,  0x83, 0, 0xcc, 0xcc, 0xcc, 1,    4,    1,
                                      0x80, 0,  17, 0,    1, 3,    6,    0,    0x10, 0,    0xfe};
    CHECK(std::equal(expected.begin(), expected.end(), fixture.bus.pif.begin() + 0x7c0));
    CHECK_EQ(fixture.poll(1), (std::vector<u8>{0x80, 0, 0, 0}));
}

TEST(mouse_device_changes_and_disconnection_clear_input_and_stop_rumble_per_port) {
    MouseFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        fixture.bus.add_mouse_input(port, {true, false, 11, 12});
    fixture.bus.add_mouse_input(4, {true, true, 999, 999});
    for (unsigned port = 0; port < 4; ++port) {
        auto state = fixture.bus.controllers()[port];
        state.connected = false;
        fixture.bus.set_controller_state(port, state);
        fixture.bus.add_mouse_input(port, {true, true, 100, 100});
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0xcc, 0xcc, 0xcc, 0xcc}));
        CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x84U);
        state.connected = true;
        fixture.bus.set_controller_state(port, state);
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0, 0, 0, 0}));
        fixture.bus.add_mouse_input(port, {true, true, 123, 124});
        state.device = ControllerDevice::Gamepad;
        state.accessory = ControllerAccessory::RumblePak;
        fixture.bus.set_controller_state(port, state);
        CHECK_EQ(fixture.status(port), 3U);
        fixture.bus.add_mouse_input(port, {false, false, 100, 100});
        fixture.command(port, {3, 0xc0, test::address_remainder(0xc000), 1}, 1);
        CHECK(fixture.bus.rumble_active(port));
        state.device = ControllerDevice::Mouse;
        fixture.bus.set_controller_state(port, state);
        CHECK(!fixture.bus.rumble_active(port));
        CHECK_EQ(fixture.poll(port), (std::vector<u8>{0, 0, 0, 0}));
        state.device = ControllerDevice::Gamepad;
        fixture.bus.set_controller_state(port, state);
        CHECK_EQ(fixture.status(port), 3U);
        for (unsigned other = port + 1; other < 4; ++other) {
            CHECK_EQ(fixture.poll(other), (std::vector<u8>{0x80, 0, 11, 0xf4}));
            fixture.bus.add_mouse_input(other, {true, false, 11, 12});
        }
    }
}

TEST(mouse_si_identification_and_motion_sampling_obey_dma_completion_boundaries) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read_dma : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true}) {
                    MouseFixture fixture;
                    test::initialize_memory(fixture.system);
                    u64 fraction = 0;
                    const auto dma = [&](u8 command, bool add_input) {
                        const std::array<u8, 1> input{command};
                        const u8 receive = command == 0 ? 3 : 4;
                        fixture.packet(port, input, receive);
                        test::configure_joybus(fixture.bus, read_dma);
                        fixture.bus.write(0x04800000, 4, 0x2000);
                        fixture.bus.write(0x04800004, 4, 0x1fc007c0);
                        const u64 rcp = 37020 + port * 1420;
                        const u64 cycles = cpu_clock ? (rcp * 3 - fraction + 1) / 2 : rcp;
                        if (cpu_clock)
                            fraction = (fraction + cycles * 2) % 3;
                        const auto advance = [&](u64 count) {
                            cpu_clock ? fixture.system.advance(count) : fixture.bus.tick(count);
                        };
                        if (single) {
                            for (u64 index = 0; index < cycles - 1; ++index)
                                advance(1);
                        } else {
                            advance(cycles - 1);
                        }
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 1U);
                        CHECK_EQ(fixture.bus.pif[fixture.response], 0xccU);
                        if (add_input)
                            fixture.bus.add_mouse_input(port, {false, true, 4, -2});
                        advance(1);
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                        CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
                        if (read_dma)
                            for (u32 index = 0; index < 64; ++index)
                                CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index),
                                         fixture.bus.pif[0x7c0 + index]);
                        fixture.bus.write(0x04800018, 4, 0);
                    };
                    fixture.bus.add_mouse_input(port, {true, false, 3, -5});
                    dma(0, false);
                    CHECK_EQ(fixture.bus.pif[fixture.response], 2U);
                    dma(1, true);
                    const std::array<u8, 4> expected{0x40, 0, 7, 7};
                    CHECK(std::equal(expected.begin(), expected.end(),
                                     fixture.bus.pif.begin() + fixture.response));
                    dma(1, false);
                    CHECK_EQ(fixture.bus.pif[fixture.response], 0x40U);
                    CHECK_EQ(fixture.bus.pif[fixture.response + 2], 0U);
                    CHECK_EQ(fixture.bus.pif[fixture.response + 3], 0U);
                }
}
