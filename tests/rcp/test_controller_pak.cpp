#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {
using namespace cupid;

struct PakFixture {
    System system;
    Bus& bus{system.bus};
    unsigned response{};

    void packet(unsigned port, std::span<const u8> input, u8 receive, u8 flags = 0) {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        bus.pif[0x7c0 + port] = static_cast<u8>(input.size()) | flags;
        bus.pif[0x7c1 + port] = receive;
        std::copy(input.begin(), input.end(), bus.pif.begin() + 0x7c2 + port);
        response = 0x7c2 + port + static_cast<unsigned>(input.size());
        std::fill_n(bus.pif.begin() + response, receive, u8{0xcc});
        bus.pif[response + receive] = 0xfe;
    }

    std::vector<u8> execute(u8 receive) {
        bus.write(0x1fc007fc, 4, 1);
        static_cast<void>(bus.read(0x1fc007fc, 4));
        return {bus.pif.begin() + response, bus.pif.begin() + response + receive};
    }

    std::vector<u8> command(unsigned port, std::initializer_list<u8> input, u8 receive, u8 flags = 0) {
        packet(port, std::span<const u8>(input.begin(), input.size()), receive, flags);
        return execute(receive);
    }

    u8 status(unsigned port) {
        return command(port, {0}, 3)[2];
    }

    void ready() {
        for (unsigned port = 0; port < 4; ++port)
            status(port);
    }

    void insert(unsigned port) {
        ControllerState state;
        state.controller_pak = false;
        bus.set_controller_state(port, state);
        state.controller_pak = true;
        bus.set_controller_state(port, state);
    }

    u8 write(unsigned port, u8 address_crc = 0) {
        std::array<u8, 35> input{};
        input[0] = 3;
        input[2] = address_crc;
        std::fill(input.begin() + 3, input.end(), u8{0x12});
        packet(port, input, 1);
        return execute(1)[0];
    }

    void blocked(unsigned port) {
        const auto read = command(port, {2, 0, 0}, 33);
        for (unsigned index = 0; index < 32; ++index)
            CHECK_EQ(read[index], 0U);
        CHECK_EQ(read[32], 0xffU);
        const auto before = bus.controller_paks[port];
        CHECK_EQ(write(port), 0xbbU);
        CHECK(bus.controller_paks[port] == before);
    }
};
} // namespace

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
        state.controller_pak = false;
        fixture.bus.set_controller_state(port, state);
        fixture.blocked(port);
        CHECK_EQ(fixture.status(port), 2U);
        fixture.blocked(port);
        CHECK_EQ(fixture.status(port), 2U);
        state.controller_pak = true;
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
                fixture.bus.write(0x1fc007fc, 4, 1);
                static_cast<void>(fixture.bus.read(0x1fc007fc, 4));
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
            state.controller_pak = pak;
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
                    if (!read) {
                        fixture.bus.pif[0x7ff] = 1;
                        for (u32 index = 0; index < 64; ++index)
                            fixture.bus.write_ram_byte(0x2000 + index, fixture.bus.pif[0x7c0 + index]);
                        std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
                    }
                    fixture.bus.write(0x04800000, 4, 0x2000);
                    fixture.bus.write(read ? 0x04800004 : 0x04800010, 4, 0x1fc007c0);
                    const u64 cycles = read ? 37020 + port * 1420 : 4065;
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
