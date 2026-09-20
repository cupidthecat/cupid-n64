#include "controller_pak_fixture.hpp"
#include "pak_crc_oracle.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;
using test::address_remainder;
using test::data_remainder;

struct BioFixture : test::PakFixture {
    BioFixture() {
        for (unsigned port = 0; port < 4; ++port) {
            ControllerState state;
            state.accessory = ControllerAccessory::BioSensor;
            bus.set_controller_state(port, state);
        }
    }

    std::vector<u8> transfer(unsigned port, u16 address, bool write = false, u8 value = 0, u8 receive = 33,
                             unsigned length = 32, u8 crc_error = 0) {
        const auto encoded = static_cast<u16>(address | address_remainder(address));
        std::vector<u8> input{write ? u8{3} : u8{2}, static_cast<u8>(encoded >> 8),
                              static_cast<u8>(encoded ^ crc_error)};
        if (write)
            input.insert(input.end(), length, value);
        packet(port, input, receive);
        auto reply = execute(receive);
        CHECK_EQ(bus.pif[response + receive], 0xfeU);
        return reply;
    }

    void expect(unsigned port, u16 address, u8 value) {
        std::array<u8, 32> data{};
        data.fill(value);
        const auto reply = transfer(port, address);
        CHECK(std::equal(data.begin(), data.end(), reply.begin()));
        CHECK_EQ(reply[32], data_remainder(data));
        CHECK_EQ(bus.pif[0x7c1 + port], 33U);
    }
};
} // namespace

TEST(bio_sensor_identification_requires_attachment_acknowledgement) {
    BioFixture fixture;
    for (unsigned port = 0; port < 4; ++port) {
        const auto rejected = fixture.transfer(port, 0x8000);
        for (unsigned index = 0; index < 32; ++index)
            CHECK_EQ(rejected[index], 0U);
        CHECK_EQ(rejected[32], 0xffU);
        CHECK_EQ(fixture.status(port), 3U);
        CHECK_EQ(fixture.status(port), 1U);
        fixture.expect(port, 0x8000, 0x81);
        fixture.expect(port, 0xc000, 3);
    }
}

TEST(bio_sensor_every_read_block_decodes_id_and_pulse_on_all_ports) {
    BioFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (bool pulse : {false, true}) {
            fixture.bus.set_bio_sensor_pulse(port, pulse);
            for (unsigned block = 0; block < 2048; ++block) {
                const auto address = static_cast<u16>(block * 32);
                fixture.expect(port, address, address < 0x8000 ? 0 : address < 0xc000 ? 0x81 : pulse ? 0 : 3);
            }
        }
}

TEST(bio_sensor_writes_acknowledge_crc_without_changing_pulse_or_saved_pak) {
    BioFixture fixture;
    fixture.ready();
    const auto storage = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (bool pulse : {false, true}) {
            fixture.bus.set_bio_sensor_pulse(port, pulse);
            for (unsigned block = 0; block < 2048; ++block) {
                const auto address = static_cast<u16>(block * 32);
                const auto value = static_cast<u8>(block);
                std::array<u8, 32> data{};
                data.fill(value);
                CHECK_EQ(fixture.transfer(port, address, true, value, 1)[0], data_remainder(data));
                fixture.expect(port, 0x8000, 0x81);
                fixture.expect(port, 0xc000, pulse ? 0 : 3);
                CHECK(!fixture.bus.rumble_active(port));
            }
        }
    CHECK(fixture.bus.controller_paks == storage);
}

TEST(bio_sensor_bad_address_crcs_reject_reads_and_writes_without_consuming_input) {
    BioFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (bool pulse : {false, true}) {
            fixture.bus.set_bio_sensor_pulse(port, pulse);
            for (u16 address : {u16{0}, u16{0x7fe0}, u16{0x8000}, u16{0xbfe0}, u16{0xc000}, u16{0xffe0}})
                for (unsigned bit = 0; bit < 5; ++bit) {
                    const auto error = static_cast<u8>(1U << bit);
                    const auto reply = fixture.transfer(port, address, false, 0, 33, 32, error);
                    for (unsigned index = 0; index < 32; ++index)
                        CHECK_EQ(reply[index], 0U);
                    CHECK_EQ(reply[32], 0xffU);
                    std::array<u8, 32> data{};
                    data.fill(0x81);
                    CHECK_EQ(fixture.transfer(port, address, true, 0x81, 1, 32, error)[0],
                             data_remainder(data) ^ 0xffU);
                    fixture.expect(port, 0xc000, pulse ? 0 : 3);
                }
        }
}

TEST(bio_sensor_short_and_padded_transfers_obey_common_pak_lengths) {
    BioFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (u8 value : {u8{0}, u8{3}, u8{0x81}}) {
            fixture.bus.set_bio_sensor_pulse(port, value == 0);
            const u16 address = value == 0x81 ? 0x8000 : 0xc000;
            std::array<u8, 32> data{};
            data.fill(value);
            for (u8 receive = 1; receive <= 40; ++receive) {
                const auto reply = fixture.transfer(port, address, false, 0, receive);
                for (unsigned index = 0; index < receive; ++index)
                    CHECK_EQ(reply[index], index < 32 ? value : index == 32 ? data_remainder(data) : 0);
                CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
            }
            for (unsigned length = 1; length <= 40; ++length) {
                const auto reply = fixture.transfer(port, address, true, value, 4, length);
                CHECK_EQ(reply[0], length < 32 ? 0U : data_remainder(data));
                for (unsigned index = 1; index < reply.size(); ++index)
                    CHECK_EQ(reply[index], 0U);
            }
            fixture.expect(port, 0xc000, value == 0 ? 0 : 3);
        }
}

TEST(bio_sensor_malformed_and_unsupported_commands_preserve_pulse) {
    BioFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port) {
        fixture.bus.set_bio_sensor_pulse(port, true);
        for (unsigned command = 0; command < 256; ++command) {
            if (command <= 3 || command == 0xff)
                continue;
            const auto reply = fixture.command(port, {static_cast<u8>(command)}, 3);
            CHECK(reply == std::vector<u8>(3, 0xcc));
            CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x83U);
        }
        for (const auto& input :
             {std::vector<u8>{2}, std::vector<u8>{2, 0xc0}, std::vector<u8>{3}, std::vector<u8>{3, 0xc0},
              std::vector<u8>{3, 0xc0, address_remainder(0xc000)}}) {
            fixture.packet(port, input, 3);
            CHECK(fixture.execute(3) == std::vector<u8>(3, 0xcc));
            CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x83U);
        }
        fixture.command(port, {2, 0xc0, address_remainder(0xc000)}, 0);
        CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0x80U);
        fixture.expect(port, 0xc000, 0);
    }
}

TEST(bio_sensor_pulse_is_a_held_input_preserved_by_polling_and_console_reset) {
    BioFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port) {
        fixture.bus.set_bio_sensor_pulse(port, true);
        fixture.expect(port, 0xc000, 0);
        auto state = fixture.bus.controllers()[port];
        state.buttons = 0x9030;
        fixture.bus.set_controller_state(port, state);
        fixture.command(port, {1}, 4);
        fixture.command(port, {0xff}, 3);
        fixture.system.reset();
        fixture.bus.tick(62500000);
        fixture.expect(port, 0xc000, 0);
        fixture.bus.set_bio_sensor_pulse(port, false);
        fixture.expect(port, 0xc000, 3);
    }
}

TEST(bio_sensor_disconnect_and_selection_changes_clear_only_the_affected_input) {
    BioFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        fixture.bus.set_bio_sensor_pulse(port, true);
    fixture.bus.set_bio_sensor_pulse(4, true);
    fixture.bus.set_bio_sensor_pulse(~0U, true);
    for (unsigned port = 0; port < 4; ++port) {
        for (unsigned action = 0; action < 4; ++action) {
            auto state = fixture.bus.controllers()[port];
            if (action == 0)
                state.connected = false;
            else if (action == 1)
                state.device = ControllerDevice::Mouse;
            else
                state.accessory = action == 2 ? ControllerAccessory::None : ControllerAccessory::RumblePak;
            fixture.bus.set_controller_state(port, state);
            fixture.bus.set_bio_sensor_pulse(port, true);
            if (action == 0) {
                const auto reply = fixture.transfer(port, 0xc000);
                CHECK(reply == std::vector<u8>(33, 0xcc));
                CHECK_EQ(fixture.bus.pif[0x7c1 + port], 0xa1U);
            }
            state.connected = true;
            state.device = ControllerDevice::Gamepad;
            state.accessory = ControllerAccessory::BioSensor;
            fixture.bus.set_controller_state(port, state);
            CHECK_EQ(fixture.status(port), 3U);
            fixture.expect(port, 0xc000, 3);
            fixture.bus.set_bio_sensor_pulse(port, true);
            for (unsigned other = 0; other < 4; ++other)
                fixture.expect(other, 0xc000, 0);
        }
    }
}

TEST(bio_sensor_si_dma_samples_pulse_at_completion_in_both_clock_domains) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read_dma : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true}) {
                    BioFixture fixture;
                    test::initialize_memory(fixture.system);
                    u64 fraction = 0;
                    const auto dma = [&](std::span<const u8> input, u8 receive, bool pulse) {
                        fixture.packet(port, input, receive);
                        fixture.bus.pif[0x7ff] = read_dma ? 0 : 1;
                        if (!read_dma) {
                            for (u32 index = 0; index < 64; ++index)
                                fixture.bus.write_ram_byte(0x2000 + index, fixture.bus.pif[0x7c0 + index]);
                            std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
                        }
                        fixture.bus.write(0x04800000, 4, 0x2000);
                        fixture.bus.write(read_dma ? 0x04800004 : 0x04800010, 4, 0x1fc007c0);
                        fixture.bus.set_bio_sensor_pulse(port, !pulse);
                        const u64 rcp = read_dma ? 37020 + port * 1420 : 4065;
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
                        CHECK_EQ(fixture.bus.pif[fixture.response], read_dma ? 0xccU : 0U);
                        fixture.bus.set_bio_sensor_pulse(port, pulse);
                        advance(1);
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                        CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
                        if (read_dma)
                            for (u32 index = 0; index < 64; ++index)
                                CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index),
                                         fixture.bus.pif[0x7c0 + index]);
                        fixture.bus.write(0x04800018, 4, 0);
                    };
                    const std::array<u8, 1> status{0};
                    dma(status, 3, false);
                    CHECK_EQ(fixture.bus.pif[fixture.response + 2], 3U);
                    const std::array<u8, 3> identify{2, 0x80, address_remainder(0x8000)};
                    dma(identify, 33, false);
                    for (unsigned index = 0; index < 32; ++index)
                        CHECK_EQ(fixture.bus.pif[fixture.response + index], 0x81U);
                    const std::array<u8, 3> read{2, 0xc0, address_remainder(0xc000)};
                    for (bool pulse : {false, true, true, false}) {
                        dma(read, 33, pulse);
                        std::array<u8, 32> data{};
                        data.fill(pulse ? 0 : 3);
                        for (unsigned index = 0; index < 32; ++index)
                            CHECK_EQ(fixture.bus.pif[fixture.response + index], data[index]);
                        CHECK_EQ(fixture.bus.pif[fixture.response + 32], data_remainder(data));
                    }
                }
}
