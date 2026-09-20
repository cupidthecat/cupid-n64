#include "controller_pak_fixture.hpp"
#include "joybus_transport.hpp"
#include "test_system.hpp"

using namespace cupid;
using test::PakFixture;

TEST(controller_pak_initial_attachment_reports_detection_once_on_each_port) {
    PakFixture fixture;
    for (unsigned port = 0; port < 4; ++port) {
        fixture.blocked(port);
        CHECK_EQ(fixture.status(port), 3U);
        CHECK_EQ(fixture.status(port), 1U);
        CHECK_EQ(fixture.write(port), 0x44U);
        const auto read = fixture.command(port, {2, 0, 0}, 33);
        for (unsigned index = 0; index < 32; ++index)
            CHECK_EQ(read[index], 0x12U);
        CHECK_EQ(read[32], 0x44U);
    }
}

TEST(controller_pak_reinsertion_gates_access_until_status_acknowledgement) {
    for (unsigned port = 0; port < 4; ++port) {
        PakFixture fixture;
        fixture.ready();
        fixture.bus.controller_paks[port].fill(0xa5);
        fixture.insert(port);
        fixture.blocked(port);
        CHECK_EQ(fixture.status(port), 3U);
        CHECK_EQ(fixture.write(port), 0x44U);
        CHECK_EQ(fixture.status(port), 1U);
        for (unsigned index = 0; index < 32; ++index)
            CHECK_EQ(fixture.bus.controller_paks[port][index], 0x12U);
        CHECK_EQ(fixture.bus.controller_paks[port][32], 0xa5U);
    }
}

TEST(controller_pak_removal_retains_storage_and_reports_absence_until_reinserted) {
    for (unsigned port = 0; port < 4; ++port) {
        PakFixture fixture;
        fixture.ready();
        fixture.bus.controller_paks[port].fill(0xa5);
        ControllerState state;
        state.accessory = ControllerAccessory::None;
        fixture.bus.set_controller_state(port, state);
        fixture.blocked(port);
        CHECK_EQ(fixture.status(port), 2U);
        fixture.blocked(port);
        CHECK_EQ(fixture.status(port), 2U);
        state.accessory = ControllerAccessory::ControllerPak;
        fixture.bus.set_controller_state(port, state);
        CHECK_EQ(fixture.status(port), 3U);
        const auto read = fixture.command(port, {2, 0, 0}, 8);
        for (u8 value : read)
            CHECK_EQ(value, 0xa5U);
    }
}

TEST(controller_pak_status_prefixes_clear_detection_even_without_a_status_byte) {
    for (unsigned port = 0; port < 4; ++port)
        for (u8 command : {u8{0}, u8{0xff}})
            for (u8 receive = 0; receive <= 4; ++receive) {
                PakFixture fixture;
                fixture.ready();
                fixture.insert(port);
                const auto reply = fixture.command(port, {command}, receive);
                const std::array<u8, 4> expected{5, 0, 3, 0};
                for (unsigned index = 0; index < receive; ++index)
                    CHECK_EQ(reply[index], expected[index]);
                CHECK_EQ(fixture.bus.pif[fixture.response + receive], 0xfeU);
                CHECK_EQ(fixture.write(port), 0x44U);
                CHECK_EQ(fixture.status(port), 1U);
            }
}

TEST(controller_pak_input_updates_and_other_ports_do_not_change_detection) {
    PakFixture fixture;
    fixture.ready();
    for (unsigned changed = 0; changed < 4; ++changed) {
        fixture.insert(changed);
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState state;
            state.buttons = static_cast<u16>(0x8100 + port);
            state.stick_x = 12;
            state.stick_y = -13;
            fixture.bus.set_controller_state(port, state);
            fixture.bus.set_controller_state(port, state);
            const auto poll = fixture.command(port, {1}, 4);
            CHECK_EQ(poll[0], 0x81U);
            CHECK_EQ(poll[1], port);
            CHECK_EQ(poll[2], 12U);
            CHECK_EQ(poll[3], 0xf3U);
            if (port != changed)
                CHECK_EQ(fixture.status(port), 1U);
        }
        fixture.blocked(changed);
        CHECK_EQ(fixture.status(changed), 3U);
    }
}

TEST(controller_pak_skips_resets_and_unrelated_commands_do_not_acknowledge_detection) {
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned action = 0; action < 7; ++action) {
            PakFixture fixture;
            fixture.ready();
            fixture.insert(port);
            if (action < 3) {
                fixture.command(port, {0}, 3, static_cast<u8>((action + 1) << 6));
            } else if (action == 3) {
                fixture.command(port, {1}, 4);
            } else if (action == 4) {
                fixture.command(port, {0x7f}, 3);
            } else {
                std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
                fixture.bus.pif[0x7c0 + port] = action == 5 ? 0xfd : 0;
                fixture.bus.pif[0x7c1 + port] = 0xfe;
                fixture.bus.joybus.configure();
                fixture.bus.joybus.execute();
            }
            fixture.blocked(port);
            CHECK_EQ(fixture.status(port), 3U);
        }
}

TEST(controller_pak_system_reset_preserves_both_pending_and_acknowledged_detection) {
    for (bool pending : {false, true}) {
        PakFixture fixture;
        fixture.ready();
        for (unsigned port = 0; port < 4; ++port) {
            fixture.bus.controller_paks[port][123] = static_cast<u8>(0xa0 + port);
            if (pending)
                fixture.insert(port);
        }
        fixture.system.reset();
        for (unsigned port = 0; port < 4; ++port) {
            CHECK_EQ(fixture.status(port), pending ? 3U : 1U);
            CHECK_EQ(fixture.bus.controller_paks[port][123], 0xa0U + port);
        }
    }
}

TEST(controller_pak_controller_reconnection_reestablishes_attachment_detection) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool pak : {false, true}) {
            PakFixture fixture;
            fixture.ready();
            ControllerState state;
            state.connected = false;
            state.accessory = pak ? ControllerAccessory::ControllerPak : ControllerAccessory::None;
            fixture.bus.set_controller_state(port, state);
            const auto reply = fixture.command(port, {0}, 3);
            CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x83U);
            for (u8 value : reply)
                CHECK_EQ(value, 0xccU);
            state.connected = true;
            fixture.bus.set_controller_state(port, state);
            CHECK_EQ(fixture.status(port), pak ? 3U : 2U);
            CHECK_EQ(fixture.status(port), pak ? 1U : 2U);
        }
}

TEST(controller_pak_bad_address_crc_does_not_clear_or_reassert_detection) {
    PakFixture fixture;
    fixture.ready();
    fixture.insert(0);
    CHECK_EQ(fixture.write(0, 1), 0xbbU);
    CHECK_EQ(fixture.status(0), 3U);
    CHECK_EQ(fixture.write(0, 1), 0xbbU);
    CHECK_EQ(fixture.status(0), 1U);
    CHECK_EQ(fixture.write(0), 0x44U);
    CHECK_EQ(fixture.status(0), 1U);
}

TEST(controller_pak_si_dma_acknowledges_detection_at_completion_under_all_clock_steps) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true}) {
                    PakFixture fixture;
                    test::initialize_memory(fixture.system);
                    fixture.ready();
                    fixture.insert(port);
                    const std::array<u8, 1> input{0};
                    fixture.packet(port, input, 3);
                    test::configure_joybus(fixture.bus, read);
                    fixture.bus.write(0x04800000, 4, 0x2000);
                    fixture.bus.write(0x04800004, 4, 0x1fc007c0);
                    const u64 cycles = 37020 + port * 1420;
                    const u64 elapsed = cpu_clock ? (cycles * 3 + 1) / 2 : cycles;
                    const auto advance = [&](u64 amount) {
                        if (cpu_clock)
                            fixture.system.advance(amount);
                        else
                            fixture.bus.tick(amount);
                    };
                    if (single) {
                        for (u64 index = 0; index < elapsed - 1; ++index)
                            advance(1);
                    } else {
                        advance(elapsed - 1);
                    }
                    CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 1U);
                    advance(1);
                    CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                    CHECK_EQ(fixture.bus.pif[fixture.response + 2], 3U);
                    if (read)
                        CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + fixture.response + 2 - 0x7c0), 3U);
                    CHECK_EQ(fixture.status(port), 1U);
                }
}
