#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {
using namespace cupid;

struct JoybusFixture {
    System system;
    Bus& bus{system.bus};
    unsigned cursor{};

    JoybusFixture() {
        test::initialize_memory(system);
        clear();
    }

    void clear() {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        cursor = 0;
    }

    void byte(u8 value) {
        CHECK(cursor < 63);
        bus.pif[0x7c0 + cursor++] = value;
    }

    unsigned packet(std::initializer_list<u8> input, u8 receive, u8 flags = 0) {
        byte(static_cast<u8>(input.size()) | flags);
        byte(receive);
        for (u8 value : input)
            byte(value);
        const unsigned response = cursor;
        for (unsigned index = 0; index < (receive & 0x3fU); ++index)
            byte(0xcc);
        return response;
    }

    void execute() {
        bus.write(0x1fc007fc, 4, 1);
        static_cast<void>(bus.read(0x1fc007fc, 4));
    }

    u8 at(unsigned offset) const {
        return bus.pif[0x7c0 + offset];
    }
};
} // namespace

TEST(joybus_skip_and_reset_flags_consume_packets_without_running_their_commands) {
    for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}})
        for (unsigned port = 0; port < 4; ++port) {
            JoybusFixture fixture;
            fixture.bus.set_save_type(SaveType::Eeprom4K);
            for (unsigned index = 0; index < port; ++index)
                fixture.byte(0);
            const unsigned ignored = fixture.packet({0x00}, 0xc3, flags);
            for (unsigned index = port + 1; index < 4; ++index)
                fixture.packet({0x00}, 3);
            const unsigned cartridge = fixture.packet({0x00}, 3);
            fixture.byte(0xfe);
            fixture.execute();
            CHECK_EQ(fixture.at(ignored - 2), 0xc3U);
            for (unsigned index = 0; index < 3; ++index)
                CHECK_EQ(fixture.at(ignored + index), 0xccU);
            for (unsigned index = port + 1; index < 4; ++index) {
                const unsigned response = ignored + (index - port) * 6;
                CHECK_EQ(fixture.at(response), 5U);
                CHECK_EQ(fixture.at(response + 1), 0U);
                CHECK_EQ(fixture.at(response + 2), 1U);
            }
            CHECK_EQ(fixture.at(cartridge), 0U);
            CHECK_EQ(fixture.at(cartridge + 1), 0x80U);
            CHECK_EQ(fixture.at(cartridge + 2), 0U);
        }
}

TEST(joybus_flagged_cartridge_writes_leave_eeprom_and_receive_flags_unchanged) {
    for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}}) {
        JoybusFixture fixture;
        fixture.bus.set_save_type(SaveType::Eeprom4K);
        for (unsigned index = 0; index < 4; ++index)
            fixture.byte(0);
        const unsigned response = fixture.packet({0x05, 0, 0x12}, 0xc1, flags);
        fixture.byte(0xfe);
        fixture.execute();
        CHECK_EQ(fixture.at(response), 0xccU);
        CHECK_EQ(fixture.at(response - 4), 0xc1U);
        CHECK_EQ(fixture.bus.eeprom[0], 0xffU);
        fixture.clear();
        for (unsigned index = 0; index < 4; ++index)
            fixture.byte(0);
        const unsigned status = fixture.packet({0x00}, 3);
        fixture.execute();
        CHECK_EQ(fixture.at(status + 2), 0U);
    }
}

TEST(joybus_controller_status_and_poll_return_prefixes_and_zero_padded_replies) {
    for (unsigned port = 0; port < 4; ++port)
        for (u8 command : {u8{0x00}, u8{0xff}, u8{0x01}})
            for (u8 receive = 0; receive <= 8; ++receive) {
                JoybusFixture fixture;
                ControllerState controller;
                controller.buttons = 0xa123;
                controller.stick_x = -7;
                controller.stick_y = 31;
                controller.controller_pak = (port & 1U) == 0;
                fixture.bus.set_controller_state(port, controller);
                for (unsigned index = 0; index < port; ++index)
                    fixture.byte(0);
                const unsigned response = fixture.packet({command}, receive | 0xc0U);
                fixture.byte(0xfe);
                fixture.execute();
                const std::array<u8, 8> expected =
                    command == 1
                        ? std::array<u8, 8>{0xa1, 0x23, 0xf9, 31, 0, 0, 0, 0}
                        : std::array<u8, 8>{5, 0, controller.controller_pak ? u8{1} : u8{2}, 0, 0, 0, 0, 0};
                for (unsigned index = 0; index < receive; ++index)
                    CHECK_EQ(fixture.at(response + index), expected[index]);
                CHECK_EQ(fixture.at(response - 2), receive | (command == 1 && receive > 4 ? 0x40U : 0U));
                CHECK_EQ(fixture.at(response + receive), 0xfeU);
            }
}

TEST(joybus_absent_and_unknown_devices_preserve_replies_and_replace_stale_error_flags) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool absent : {false, true}) {
            JoybusFixture fixture;
            ControllerState controller;
            controller.connected = !absent;
            fixture.bus.set_controller_state(port, controller);
            for (unsigned index = 0; index < port; ++index)
                fixture.byte(0);
            const unsigned response = fixture.packet({absent ? u8{0x00} : u8{0x7f}}, 0x43);
            fixture.byte(0xfe);
            fixture.execute();
            CHECK_EQ(fixture.at(response - 2), 0x83U);
            for (unsigned index = 0; index < 3; ++index)
                CHECK_EQ(fixture.at(response + index), 0xccU);
        }
}

TEST(joybus_packet_payloads_cannot_include_the_pif_control_byte) {
    for (unsigned end : {63U, 64U, 65U}) {
        JoybusFixture fixture;
        std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0xff});
        fixture.bus.pif[0x7f7] = 1;
        fixture.bus.pif[0x7f8] = static_cast<u8>(end - 58);
        fixture.bus.pif[0x7f9] = 0;
        std::fill(fixture.bus.pif.begin() + 0x7fa, fixture.bus.pif.begin() + 0x7ff, u8{0xcc});
        fixture.bus.pif[0x7ff] = 0;
        fixture.bus.write(0x04800000, 4, 0x2000);
        fixture.bus.write(0x04800004, 4, 0x1fc007c0);
        fixture.bus.tick(200000);
        CHECK_EQ(fixture.at(58), end == 63 ? 5U : 0xccU);
        CHECK_EQ(fixture.at(59), end == 63 ? 0U : 0xccU);
        CHECK_EQ(fixture.at(60), end == 63 ? 1U : 0xccU);
        CHECK_EQ(fixture.at(63), 0U);
        CHECK_EQ(fixture.bus.read_ram_byte(0x203a), fixture.at(58));
    }
}

TEST(joybus_padding_channel_markers_and_end_markers_keep_ports_aligned) {
    JoybusFixture fixture;
    fixture.byte(0xff);
    fixture.byte(0);
    fixture.byte(0xff);
    fixture.byte(0xfd);
    const unsigned third = fixture.packet({0x00}, 3);
    const unsigned fourth = fixture.packet({0x01}, 4);
    fixture.byte(0xfe);
    const unsigned ignored = fixture.packet({0x00}, 3);
    ControllerState controller;
    controller.buttons = 0x8123;
    controller.stick_x = -128;
    controller.stick_y = 127;
    fixture.bus.set_controller_state(3, controller);
    fixture.execute();
    CHECK_EQ(fixture.at(third), 5U);
    CHECK_EQ(fixture.at(fourth), 0x81U);
    CHECK_EQ(fixture.at(fourth + 1), 0x23U);
    CHECK_EQ(fixture.at(fourth + 2), 0x80U);
    CHECK_EQ(fixture.at(fourth + 3), 0x7fU);
    CHECK_EQ(fixture.at(ignored), 0xccU);
}

TEST(joybus_flagged_controller_pak_writes_preserve_every_port_and_later_packets) {
    for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}})
        for (unsigned port = 0; port < 4; ++port) {
            JoybusFixture fixture;
            fixture.bus.set_save_type(SaveType::Eeprom4K);
            for (auto& pak : fixture.bus.controller_paks)
                std::fill(pak.begin(), pak.end(), u8{0xa5});
            for (unsigned index = 0; index < port; ++index)
                fixture.byte(0);
            fixture.byte(35U | flags);
            fixture.byte(0xc1);
            fixture.byte(0x03);
            fixture.byte(0);
            fixture.byte(0);
            for (unsigned index = 0; index < 32; ++index)
                fixture.byte(static_cast<u8>(index));
            const unsigned ignored = fixture.cursor;
            fixture.byte(0xcc);
            for (unsigned index = port + 1; index < 4; ++index)
                fixture.byte(0);
            const unsigned status = fixture.packet({0x00}, 3);
            fixture.execute();
            CHECK_EQ(fixture.at(ignored), 0xccU);
            CHECK_EQ(fixture.at(status + 1), 0x80U);
            for (const auto& pak : fixture.bus.controller_paks)
                CHECK(std::all_of(pak.begin(), pak.end(), [](u8 value) { return value == 0xa5; }));
        }
}

TEST(joybus_short_replies_do_not_modify_the_following_port_packet) {
    for (u8 command : {u8{0x00}, u8{0x01}})
        for (u8 receive = 0; receive < 3; ++receive) {
            JoybusFixture fixture;
            const unsigned first = fixture.packet({command}, receive);
            const unsigned second = fixture.packet({0x01}, 4);
            ControllerState controller;
            controller.buttons = 0x8123;
            controller.stick_x = 7;
            controller.stick_y = -9;
            fixture.bus.set_controller_state(1, controller);
            fixture.byte(0xfe);
            fixture.execute();
            CHECK_EQ(fixture.at(first - 2), receive);
            CHECK_EQ(fixture.at(second - 3), 1U);
            CHECK_EQ(fixture.at(second - 2), 4U);
            CHECK_EQ(fixture.at(second - 1), 1U);
            CHECK_EQ(fixture.at(second), 0x81U);
            CHECK_EQ(fixture.at(second + 1), 0x23U);
            CHECK_EQ(fixture.at(second + 2), 7U);
            CHECK_EQ(fixture.at(second + 3), 0xf7U);
        }
}

TEST(joybus_zero_payload_flagged_packets_advance_to_the_next_channel) {
    for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}}) {
        JoybusFixture fixture;
        fixture.byte(flags);
        fixture.byte(0xc0);
        const unsigned response = fixture.packet({0x00}, 3);
        fixture.byte(0xfe);
        fixture.execute();
        CHECK_EQ(fixture.at(1), 0xc0U);
        CHECK_EQ(fixture.at(response), 5U);
        CHECK_EQ(fixture.at(response + 1), 0U);
        CHECK_EQ(fixture.at(response + 2), 1U);
    }
}

TEST(joybus_cpu_and_both_si_dma_directions_preserve_packets_across_clock_step_sizes) {
    std::array<u8, 64> expected{};
    bool first = true;
    for (unsigned transport = 0; transport < 3; ++transport)
        for (bool cpu_clock : {false, true})
            for (bool single_cycle : {false, true}) {
                JoybusFixture fixture;
                fixture.bus.set_save_type(SaveType::Eeprom4K);
                fixture.packet({0x00}, 3, 0x80);
                const unsigned poll = fixture.packet({0x01}, 2);
                fixture.byte(0);
                fixture.byte(0xfd);
                const unsigned cartridge = fixture.packet({0x00}, 3);
                fixture.byte(0xfe);
                ControllerState controller;
                controller.buttons = 0x9123;
                fixture.bus.set_controller_state(1, controller);
                fixture.bus.pif[0x7ff] = transport == 2 ? 1 : 0;
                u64 cycles = 0;
                if (transport == 0) {
                    fixture.execute();
                } else {
                    if (transport == 2) {
                        for (u32 index = 0; index < 64; ++index)
                            fixture.bus.write_ram_byte(0x2000 + index, fixture.at(index));
                        std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
                    }
                    fixture.bus.write(0x04800000, 4, 0x2000);
                    fixture.bus.write(transport == 1 ? 0x04800004 : 0x04800010, 4, 0x1fc007c0);
                    cycles = transport == 1 ? 80440 : 4065;
                }
                const u64 elapsed = cpu_clock ? (cycles * 3 + 1) / 2 : cycles;
                const auto advance = [&](u64 count) {
                    if (cpu_clock)
                        fixture.system.advance(count);
                    else
                        fixture.bus.tick(count);
                };
                if (single_cycle) {
                    for (u64 index = 0; index < elapsed; ++index)
                        advance(1);
                } else {
                    advance(elapsed);
                }
                CHECK_EQ(fixture.at(poll), 0x91U);
                CHECK_EQ(fixture.at(poll + 1), 0x23U);
                CHECK_EQ(fixture.at(cartridge + 1), 0x80U);
                if (transport != 0) {
                    CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                    if (transport == 1)
                        for (u32 index = 0; index < 64; ++index)
                            CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index), fixture.at(index));
                }
                if (first) {
                    std::copy_n(fixture.bus.pif.begin() + 0x7c0, 64, expected.begin());
                    first = false;
                } else {
                    CHECK(std::equal(expected.begin(), expected.end(), fixture.bus.pif.begin() + 0x7c0));
                }
            }
}

TEST(joybus_pak_replies_zero_pad_after_the_data_and_crc) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool write : {false, true}) {
            JoybusFixture fixture;
            std::fill(fixture.bus.controller_paks[port].begin(), fixture.bus.controller_paks[port].end(),
                      u8{0});
            for (unsigned index = 0; index < port; ++index)
                fixture.byte(0);
            fixture.byte(write ? 35 : 3);
            fixture.byte(write ? 8 : 40);
            fixture.byte(write ? 0x03 : 0x02);
            fixture.byte(0);
            fixture.byte(0);
            if (write)
                for (unsigned index = 0; index < 32; ++index)
                    fixture.byte(0);
            const unsigned response = fixture.cursor;
            const unsigned length = write ? 8 : 40;
            for (unsigned index = 0; index < length; ++index)
                fixture.byte(0xcc);
            fixture.byte(0xfe);
            fixture.execute();
            for (unsigned index = 0; index < length; ++index)
                CHECK_EQ(fixture.at(response + index), 0U);
            CHECK_EQ(fixture.at(response + length), 0xfeU);
        }
}

TEST(joybus_four_port_polls_keep_each_device_and_error_response_isolated) {
    for (unsigned absent = 0; absent <= 4; ++absent) {
        JoybusFixture fixture;
        fixture.bus.set_save_type(SaveType::Eeprom16K);
        std::array<unsigned, 4> responses{};
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState controller;
            controller.connected = port != absent;
            controller.buttons = static_cast<u16>(0x8120 + port);
            controller.stick_x = static_cast<s8>(-10 - static_cast<int>(port));
            controller.stick_y = static_cast<s8>(20 + port);
            fixture.bus.set_controller_state(port, controller);
            responses[port] = fixture.packet({0x01}, 4);
        }
        const unsigned cartridge = fixture.packet({0x00}, 3);
        fixture.execute();
        for (unsigned port = 0; port < 4; ++port) {
            const unsigned response = responses[port];
            CHECK_EQ(fixture.at(response - 2), port == absent ? 0x84U : 4U);
            CHECK_EQ(fixture.at(response), port == absent ? 0xccU : 0x81U);
            CHECK_EQ(fixture.at(response + 1), port == absent ? 0xccU : 0x20U + port);
            CHECK_EQ(fixture.at(response + 2), port == absent ? 0xccU : 246U - port);
            CHECK_EQ(fixture.at(response + 3), port == absent ? 0xccU : 20U + port);
        }
        CHECK_EQ(fixture.at(cartridge + 1), 0xc0U);
    }
}
