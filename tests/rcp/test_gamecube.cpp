#include "cupid/system.hpp"
#include "joybus_transport.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <tuple>
#include <vector>

namespace {
using namespace cupid;

struct GameCubeFixture {
    System system;
    Bus& bus{system.bus};
    unsigned recv_offset{};
    unsigned response{};

    GameCubeFixture() {
        test::initialize_memory(system);
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState controller;
            controller.device = ControllerDevice::GameCube;
            bus.set_controller_state(port, controller);
        }
    }

    void clear() {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    }

    std::vector<u8> command(unsigned port, std::initializer_list<u8> input, u8 recv, u8 flags = 0) {
        clear();
        unsigned offset = port;
        bus.pif[0x7c0 + offset++] = static_cast<u8>(input.size()) | flags;
        recv_offset = 0x7c0 + offset;
        bus.pif[0x7c0 + offset++] = recv;
        for (u8 value : input)
            bus.pif[0x7c0 + offset++] = value;
        response = 0x7c0 + offset;
        std::fill_n(bus.pif.begin() + response, recv, u8{0xcc});
        offset += recv;
        bus.pif[0x7c0 + offset] = 0xfe;
        bus.joybus.configure();
        bus.joybus.execute();
        return {bus.pif.begin() + response, bus.pif.begin() + response + recv};
    }

    void configure_reset(unsigned port, unsigned repeats = 1) {
        clear();
        bus.pif[0x7c0 + port] = 0xfd;
        bus.pif[0x7c1 + port] = 0xfe;
        for (unsigned index = 0; index < repeats; ++index)
            bus.joybus.configure();
    }

    u8 receive() const {
        return bus.pif[recv_offset];
    }
};

GameCubeState sample_state() {
    GameCubeState state;
    state.buttons0 = 0x95;
    state.buttons1 = 0xd5;
    state.stick_x = 0x12;
    state.stick_y = 0x34;
    state.cstick_x = 0xab;
    state.cstick_y = 0xcd;
    state.trigger_l = 0xe1;
    state.trigger_r = 0x72;
    return state;
}

constexpr std::array<std::array<u8, 8>, 8> ExpectedModes{{
    {0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe7, 0x00},
    {0x35, 0xd5, 0x12, 0x34, 0xac, 0xe1, 0x72, 0x00},
    {0x35, 0xd5, 0x12, 0x34, 0xac, 0xe7, 0x00, 0x00},
    {0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72},
    {0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0x00, 0x00},
    {0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe7, 0x00},
    {0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe7, 0x00},
    {0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe7, 0x00},
}};

void clear_origin(GameCubeFixture& fixture, unsigned port) {
    CHECK_EQ(fixture.command(port, {0x41}, 10), (std::vector<u8>{0, 0x80, 127, 127, 127, 127, 0, 0, 0, 0}));
}

} // namespace

TEST(gamecube_all_ports_and_analog_modes_return_exact_wire_state) {
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned mode = 0; mode < 8; ++mode) {
            GameCubeFixture fixture;
            fixture.bus.set_gamecube_state(port, sample_state());
            const auto reply = fixture.command(port, {0x40, static_cast<u8>(mode), 0}, 8);
            const auto& expected = ExpectedModes[mode];
            CHECK(std::equal(expected.begin(), expected.end(), reply.begin()));
            CHECK_EQ(fixture.receive(), 8U);
        }
    for (unsigned port = 0; port < 4; ++port)
        for (u8 mode : {u8{0x09}, u8{0xff}}) {
            GameCubeFixture fixture;
            fixture.bus.set_gamecube_state(port, sample_state());
            const auto reply = fixture.command(port, {0x40, mode, 0}, 8);
            CHECK(std::equal(ExpectedModes[0].begin(), ExpectedModes[0].end(), reply.begin()));
        }
}

TEST(gamecube_status_origin_and_long_read_use_the_raw_wire_contract) {
    for (unsigned port = 0; port < 4; ++port) {
        GameCubeFixture fixture;
        auto state = sample_state();
        state.buttons0 = 0xff;
        state.buttons1 = 0xff;
        fixture.bus.set_gamecube_state(port, state);
        CHECK_EQ(fixture.command(port, {0x00}, 4), (std::vector<u8>{0x09, 0, 0, 0}));
        CHECK_EQ(fixture.receive(), 4U);
        CHECK_EQ(fixture.command(port, {0x43}, 10),
                 (std::vector<u8>{0x3f, 0xff, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72, 0, 0}));
        for (u8 command : {u8{0x41}, u8{0x42}}) {
            CHECK_EQ(fixture.command(port, {command}, 10),
                     (std::vector<u8>{0, 0x80, 127, 127, 127, 127, 0, 0, 0, 0}));
            CHECK_EQ(fixture.receive(), 10U);
            CHECK_EQ(fixture.command(port, {0x43}, 10),
                     (std::vector<u8>{0x1f, 0xff, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72, 0, 0}));
            auto controller = fixture.bus.controllers()[port];
            controller.connected = false;
            fixture.bus.set_controller_state(port, controller);
            controller.connected = true;
            fixture.bus.set_controller_state(port, controller);
        }
        CHECK_EQ(fixture.command(port, {0x40, 3, 1}, 8),
                 (std::vector<u8>{0x3f, 0xff, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72}));
        CHECK(fixture.bus.rumble_active(port));
        CHECK_EQ(fixture.command(port, {0xff}, 3), (std::vector<u8>{0x09, 0, 0x08}));
    }
}

TEST(gamecube_length_boundaries_overflow_and_invalid_commands_match_joybus_flags) {
    for (unsigned port = 0; port < 4; ++port) {
        {
            GameCubeFixture fixture;
            fixture.bus.set_gamecube_state(port, sample_state());
            CHECK(fixture.command(port, {0x40, 3, 0}, 0).empty());
            CHECK_EQ(fixture.receive(), 0U);
            CHECK_EQ(fixture.command(port, {0x40, 3, 0}, 3), (std::vector<u8>{0x35, 0xd5, 0x12}));
            CHECK_EQ(fixture.receive(), 3U);
        }
        {
            GameCubeFixture fixture;
            CHECK(fixture.command(port, {0x41}, 0).empty());
            CHECK_EQ(fixture.receive(), 0U);
            CHECK_EQ(fixture.command(port, {0x43}, 2), (std::vector<u8>{0, 0x80}));
            CHECK_EQ(fixture.receive(), 2U);
        }

        GameCubeFixture fixture;
        fixture.bus.set_gamecube_state(port, sample_state());
        for (const auto& [input, recv, expected_flag, reply_length] : {
                 std::tuple{std::vector<u8>{0x40, 3, 0}, u8{8}, u8{8}, 8U},
                 std::tuple{std::vector<u8>{0x40, 3, 0}, u8{9}, u8{0x49}, 8U},
                 std::tuple{std::vector<u8>{0x41}, u8{10}, u8{10}, 10U},
                 std::tuple{std::vector<u8>{0x41}, u8{11}, u8{0x4b}, 10U},
                 std::tuple{std::vector<u8>{0x42}, u8{11}, u8{0x4b}, 10U},
                 std::tuple{std::vector<u8>{0x43}, u8{10}, u8{10}, 10U},
                 std::tuple{std::vector<u8>{0x43}, u8{11}, u8{0x4b}, 10U},
             }) {
            fixture.clear();
            unsigned offset = port;
            fixture.bus.pif[0x7c0 + offset++] = static_cast<u8>(input.size());
            fixture.recv_offset = 0x7c0 + offset;
            fixture.bus.pif[0x7c0 + offset++] = recv;
            for (u8 value : input)
                fixture.bus.pif[0x7c0 + offset++] = value;
            fixture.response = 0x7c0 + offset;
            std::fill_n(fixture.bus.pif.begin() + fixture.response, recv, u8{0xcc});
            fixture.bus.pif[0x7c0 + offset + recv] = 0xfe;
            fixture.bus.joybus.configure();
            fixture.bus.joybus.execute();
            CHECK_EQ(fixture.receive(), expected_flag);
            if (recv > reply_length)
                CHECK_EQ(fixture.bus.pif[fixture.response + reply_length], 0U);
        }

        for (const auto& input : {std::vector<u8>{0x40}, std::vector<u8>{0x40, 3}, std::vector<u8>{0x01}}) {
            fixture.clear();
            unsigned offset = port;
            fixture.bus.pif[0x7c0 + offset++] = static_cast<u8>(input.size());
            fixture.recv_offset = 0x7c0 + offset;
            fixture.bus.pif[0x7c0 + offset++] = 8;
            for (u8 value : input)
                fixture.bus.pif[0x7c0 + offset++] = value;
            fixture.response = 0x7c0 + offset;
            std::fill_n(fixture.bus.pif.begin() + fixture.response, 8, u8{0xcc});
            fixture.bus.pif[0x7c0 + offset + 8] = 0xfe;
            fixture.bus.joybus.configure();
            fixture.bus.joybus.execute();
            CHECK_EQ(fixture.receive(), 0x88U);
            CHECK(std::all_of(fixture.bus.pif.begin() + fixture.response,
                              fixture.bus.pif.begin() + fixture.response + 8,
                              [](u8 value) { return value == 0xcc; }));
        }
    }
}

TEST(gamecube_pif_reset_forms_rearm_origin_and_stop_only_the_selected_motor) {
    for (unsigned target = 0; target < 4; ++target) {
        GameCubeFixture fixture;
        for (unsigned port = 0; port < 4; ++port) {
            fixture.bus.set_gamecube_state(port, sample_state());
            clear_origin(fixture, port);
            fixture.command(port, {0x40, 3, 1}, 8);
            CHECK(fixture.bus.rumble_active(port));
        }

        fixture.configure_reset(target, 2);
        CHECK(fixture.bus.rumble_active(target));
        fixture.bus.joybus.execute();
        CHECK(!fixture.bus.rumble_active(target));
        for (unsigned port = 0; port < 4; ++port) {
            const auto reply = fixture.command(port, {0x43}, 10);
            CHECK_EQ(reply[0] & 0x20U, port == target ? 0x20U : 0U);
            CHECK_EQ(fixture.bus.rumble_active(port), port != target);
        }

        clear_origin(fixture, target);
        fixture.command(target, {0x40, 3, 1}, 8);
        fixture.command(target, {0x00}, 3, 0xc0);
        CHECK(fixture.bus.rumble_active(target));
        CHECK_EQ(fixture.command(target, {0x43}, 10)[0] & 0x20U, 0U);
        fixture.command(target, {0x00}, 3, 0x40);
        CHECK(!fixture.bus.rumble_active(target));
        CHECK_EQ(fixture.command(target, {0x43}, 10)[0] & 0x20U, 0x20U);
    }
}

TEST(gamecube_hotplug_and_type_changes_reset_protocol_state_without_erasing_raw_input) {
    for (unsigned port = 0; port < 4; ++port) {
        GameCubeFixture fixture;
        fixture.bus.set_gamecube_state(port, sample_state());
        clear_origin(fixture, port);
        fixture.command(port, {0x40, 3, 1}, 8);
        auto controller = fixture.bus.controllers()[port];
        controller.connected = false;
        fixture.bus.set_controller_state(port, controller);
        CHECK(!fixture.bus.rumble_active(port));
        controller.connected = true;
        fixture.bus.set_controller_state(port, controller);
        CHECK_EQ(fixture.command(port, {0x43}, 10),
                 (std::vector<u8>{0x35, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72, 0, 0}));

        clear_origin(fixture, port);
        fixture.command(port, {0x40, 3, 1}, 8);
        controller.device = ControllerDevice::Mouse;
        fixture.bus.set_controller_state(port, controller);
        CHECK(!fixture.bus.rumble_active(port));
        controller.device = ControllerDevice::GameCube;
        fixture.bus.set_controller_state(port, controller);
        CHECK_EQ(fixture.command(port, {0x43}, 10)[0], 0x35U);
    }
}

TEST(gamecube_n64_accessory_changes_do_not_affect_the_attached_controller) {
    for (unsigned port = 0; port < 4; ++port) {
        GameCubeFixture fixture;
        fixture.bus.set_gamecube_state(port, sample_state());
        clear_origin(fixture, port);
        fixture.command(port, {0x40, 3, 1}, 8);
        for (ControllerAccessory accessory :
             {ControllerAccessory::None, ControllerAccessory::ControllerPak, ControllerAccessory::RumblePak,
              ControllerAccessory::BioSensor, ControllerAccessory::TransferPak}) {
            auto controller = fixture.bus.controllers()[port];
            controller.accessory = accessory;
            fixture.bus.set_controller_state(port, controller);
            CHECK(fixture.bus.rumble_active(port));
            CHECK_EQ(fixture.command(port, {0x43}, 10),
                     (std::vector<u8>{0x15, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72, 0, 0}));
        }
    }
}

TEST(gamecube_console_reset_preserves_attached_pad_state_until_a_joybus_reset) {
    GameCubeFixture fixture;
    fixture.bus.set_gamecube_state(2, sample_state());
    clear_origin(fixture, 2);
    fixture.command(2, {0x40, 3, 1}, 8);
    fixture.system.reset();
    test::initialize_memory(fixture.system);
    CHECK(fixture.bus.rumble_active(2));
    CHECK_EQ(fixture.command(2, {0x43}, 10),
             (std::vector<u8>{0x15, 0xd5, 0x12, 0x34, 0xab, 0xcd, 0xe1, 0x72, 0, 0}));
}

TEST(gamecube_pif_resets_are_deferred_until_si_read_completion) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool raw_reset : {false, true})
            for (bool cpu_config : {false, true}) {
                GameCubeFixture fixture;
                fixture.bus.set_gamecube_state(port, sample_state());
                clear_origin(fixture, port);
                fixture.command(port, {0x40, 3, 1}, 8);
                CHECK(fixture.bus.rumble_active(port));

                fixture.clear();
                unsigned offset = port;
                if (raw_reset) {
                    fixture.bus.pif[0x7c0 + offset++] = 0x41;
                    fixture.bus.pif[0x7c0 + offset++] = 3;
                    fixture.bus.pif[0x7c0 + offset++] = 0;
                    std::fill_n(fixture.bus.pif.begin() + 0x7c0 + offset, 3, u8{0xcc});
                    offset += 3;
                } else {
                    fixture.bus.pif[0x7c0 + offset++] = 0xfd;
                }
                fixture.bus.pif[0x7c0 + offset] = 0xfe;
                test::configure_joybus(fixture.bus, cpu_config);
                CHECK(fixture.bus.rumble_active(port));

                fixture.bus.write(0x04800000, 4, 0x2000);
                fixture.bus.write(0x04800004, 4, 0x1fc007c0);
                const u64 delay = (raw_reset ? 37020U : 16440U) + port * 1420U;
                fixture.bus.tick(delay - 1);
                CHECK(fixture.bus.rumble_active(port));
                CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 1U);
                fixture.bus.tick(1);
                CHECK(!fixture.bus.rumble_active(port));
                CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                CHECK_EQ(fixture.command(port, {0x43}, 10)[0] & 0x20U, 0x20U);
            }
}

TEST(gamecube_si_transport_samples_raw_state_and_rumble_at_the_read_completion_boundary) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool cpu_config : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true}) {
                    GameCubeFixture fixture;
                    fixture.bus.set_gamecube_state(port, GameCubeState{});
                    fixture.clear();
                    unsigned offset = port;
                    fixture.bus.pif[0x7c0 + offset++] = 3;
                    fixture.bus.pif[0x7c0 + offset++] = 8;
                    fixture.bus.pif[0x7c0 + offset++] = 0x40;
                    fixture.bus.pif[0x7c0 + offset++] = 3;
                    fixture.bus.pif[0x7c0 + offset++] = 1;
                    const unsigned response = 0x7c0 + offset;
                    std::fill_n(fixture.bus.pif.begin() + response, 8, u8{0xcc});
                    fixture.bus.pif[response + 8] = 0xfe;
                    test::configure_joybus(fixture.bus, cpu_config);
                    fixture.bus.write(0x04800000, 4, 0x2000);
                    fixture.bus.write(0x04800004, 4, 0x1fc007c0);
                    const u64 rcp = 37020 + port * 1420;
                    const u64 cycles = cpu_clock ? (rcp * 3 + 1) / 2 : rcp;
                    const auto advance = [&](u64 count) {
                        const auto step = [&](u64 amount) {
                            if (cpu_clock)
                                fixture.system.advance(amount);
                            else
                                fixture.bus.tick(amount);
                        };
                        if (single)
                            for (u64 index = 0; index < count; ++index)
                                step(1);
                        else
                            step(count);
                    };
                    advance(cycles - 1);
                    CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 1U);
                    CHECK_EQ(fixture.bus.pif[response], 0xccU);
                    CHECK(!fixture.bus.rumble_active(port));
                    fixture.bus.set_gamecube_state(port, sample_state());
                    advance(1);
                    CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                    const auto& expected = ExpectedModes[3];
                    CHECK(std::equal(expected.begin(), expected.end(), fixture.bus.pif.begin() + response));
                    CHECK(fixture.bus.rumble_active(port));
                    for (unsigned index = 0; index < expected.size(); ++index)
                        CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + response - 0x7c0 + index),
                                 expected[index]);
                }
}
