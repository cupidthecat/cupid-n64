#include "controller_pak_fixture.hpp"
#include "joybus_transport.hpp"
#include "pak_crc_oracle.hpp"
#include "test_system.hpp"

#include <limits>

namespace {
using namespace cupid;

std::array<u8, 35> select_packet(u8 bank) {
    std::array<u8, 35> input{3, 0x80, 1};
    std::fill(input.begin() + 3, input.end(), bank);
    return input;
}

struct BankedFixture : test::PakFixture {
    explicit BankedFixture(unsigned banks = 3) {
        CHECK_EQ(test::address_remainder(0x8000), 1U);
        for (unsigned port = 0; port < 4; ++port) {
            CHECK(bus.configure_controller_pak(port, banks));
            for (unsigned bank = 0; bank < banks; ++bank)
                std::fill_n(bus.controller_paks[port].begin() + bank * 32768U, 32,
                            static_cast<u8>(0x21U + bank + port));
        }
        ready();
    }

    void select(unsigned port, u8 bank) {
        const auto input = select_packet(bank);
        packet(port, input, 1);
        CHECK_EQ(execute(1)[0], test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
        CHECK_EQ(bus.pif[0x7c1 + port], 1U);
    }

    u8 tag(unsigned port) {
        const auto reply = command(port, {2, 0, 0}, 33);
        CHECK_EQ(bus.pif[0x7c1 + port], 33U);
        CHECK_EQ(reply[32], test::data_remainder(std::span<const u8, 32>(reply.data(), 32)));
        return reply[0];
    }
};
} // namespace

TEST(controller_pak_banks_configuration_preserves_data_and_rejects_invalid_sizes) {
    test::PakFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port) {
        CHECK_EQ(fixture.bus.controller_paks[port].size(), 32768U);
        fixture.bus.controller_paks[port][0] = static_cast<u8>(0xa0U + port);
        for (unsigned banks = 1; banks <= 62; ++banks) {
            CHECK(fixture.bus.configure_controller_pak(port, banks));
            const auto& bytes = fixture.bus.controller_paks[port];
            CHECK_EQ(bytes.size(), banks * 32768U);
            CHECK_EQ(bytes[0], 0xa0U + port);
            CHECK(std::all_of(bytes.begin() + 1, bytes.end(), [](u8 byte) { return byte == 0; }));
            CHECK_EQ(fixture.status(port), banks == 1 ? 1U : 3U);
        }
    }
    const auto expected = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned banks : {0U, 63U, std::numeric_limits<unsigned>::max()}) {
            CHECK(!fixture.bus.configure_controller_pak(port, banks));
            CHECK_EQ(fixture.bus.controller_paks, expected);
            CHECK_EQ(fixture.status(port), 1U);
        }
    for (unsigned port : {4U, std::numeric_limits<unsigned>::max()}) {
        CHECK(!fixture.bus.configure_controller_pak(port, 2));
        CHECK_EQ(fixture.bus.controller_paks, expected);
    }
}

TEST(controller_pak_banks_all_62_banks_have_independent_storage_on_all_ports) {
    BankedFixture fixture(62);
    auto expected = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (u8 bank = 0; bank < 62; ++bank) {
            fixture.select(port, bank);
            CHECK_EQ(fixture.tag(port), 0x21U + bank + port);
            const u16 encoded = static_cast<u16>(0x7fe0U | test::address_remainder(0x7fe0));
            std::array<u8, 35> input{3, static_cast<u8>(encoded >> 8), static_cast<u8>(encoded)};
            for (unsigned index = 0; index < 32; ++index)
                input[index + 3] = static_cast<u8>(port * 43U + bank * 17U + index);
            fixture.packet(port, input, 1);
            CHECK_EQ(fixture.execute(1)[0],
                     test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
            std::copy_n(input.begin() + 3, 32, expected[port].begin() + bank * 32768U + 0x7fe0);
            const auto reply = fixture.command(port, {2, input[1], input[2]}, 33);
            CHECK(std::equal(input.begin() + 3, input.end(), reply.begin()));
            CHECK_EQ(reply[32], test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
        }
    CHECK_EQ(fixture.bus.controller_paks, expected);
    for (unsigned port = 0; port < 4; ++port)
        CHECK_EQ(fixture.tag(port), 0x21U + 61U + port);
}

TEST(controller_pak_banks_selection_uses_the_first_byte_and_ignores_unavailable_banks) {
    for (unsigned banks : {1U, 2U, 16U, 62U}) {
        BankedFixture fixture(banks);
        const auto stored = fixture.bus.controller_paks;
        for (unsigned port = 0; port < 4; ++port) {
            fixture.select(port, static_cast<u8>(banks - 1));
            for (unsigned bank = banks; bank < 256; ++bank) {
                auto input = select_packet(0);
                input[3] = static_cast<u8>(bank);
                fixture.packet(port, input, 1);
                CHECK_EQ(fixture.execute(1)[0],
                         test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
                CHECK_EQ(fixture.tag(port), 0x21U + banks - 1U + port);
            }
            auto input = select_packet(0xff);
            input[3] = 0;
            fixture.packet(port, input, 1);
            CHECK_EQ(fixture.execute(1)[0],
                     test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
            CHECK_EQ(fixture.tag(port), 0x21U + port);
        }
        CHECK_EQ(fixture.bus.controller_paks, stored);
    }
}

TEST(controller_pak_banks_packet_lengths_crc_and_flags_gate_selection) {
    BankedFixture fixture;
    const auto stored = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port) {
        for (unsigned length = 1; length <= 40; ++length) {
            fixture.select(port, 0);
            std::vector<u8> input{3, 0x80, 1};
            input.insert(input.end(), length, u8{1});
            input[3] = 2;
            fixture.packet(port, input, 4);
            const auto reply = fixture.execute(4);
            CHECK_EQ(reply[0],
                     length < 32 ? 0U : test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
            CHECK_EQ(reply[1], 0U);
            CHECK_EQ(reply[2], 0U);
            CHECK_EQ(reply[3], 0U);
            CHECK_EQ(fixture.tag(port), 0x23U + port);
        }
        fixture.select(port, 1);
        for (unsigned bit = 0; bit < 5; ++bit) {
            auto input = select_packet(2);
            input[2] ^= static_cast<u8>(1U << bit);
            fixture.packet(port, input, 1);
            CHECK_EQ(fixture.execute(1)[0],
                     test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)) ^ 0xffU);
            CHECK_EQ(fixture.tag(port), 0x22U + port);
        }
        for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}}) {
            fixture.packet(port, select_packet(2), 1, flags);
            CHECK_EQ(fixture.execute(1)[0], 0xccU);
            CHECK_EQ(fixture.tag(port), 0x22U + port);
        }
        for (unsigned send = 1; send <= 4; ++send)
            for (u8 receive : {u8{0}, u8{1}}) {
                if (send == 4 && receive == 1)
                    continue;
                const auto input = select_packet(2);
                fixture.packet(port, std::span<const u8>(input).first(send), receive);
                const auto reply = fixture.execute(receive);
                CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive | 0x80U);
                CHECK(std::all_of(reply.begin(), reply.end(), [](u8 byte) { return byte == 0xcc; }));
                CHECK_EQ(fixture.tag(port), 0x22U + port);
            }
    }
    CHECK_EQ(fixture.bus.controller_paks, stored);
}

TEST(controller_pak_banks_high_addresses_never_alias_storage_or_select_another_bank) {
    BankedFixture fixture(4);
    for (unsigned port = 0; port < 4; ++port) {
        fixture.select(port, 2);
        for (u16 address : {u16{0x8020}, u16{0x9000}, u16{0xc000}, u16{0xffe0}}) {
            const auto stored = fixture.bus.controller_paks;
            const auto encoded = static_cast<u16>(address | test::address_remainder(address));
            CHECK_EQ(
                fixture.command(port, {3, static_cast<u8>(encoded >> 8), static_cast<u8>(encoded), 1}, 1)[0],
                0U);
            CHECK_EQ(fixture.bus.controller_paks, stored);
            CHECK_EQ(fixture.tag(port), 0x23U + port);
            const auto reply =
                fixture.command(port, {2, static_cast<u8>(encoded >> 8), static_cast<u8>(encoded)}, 40);
            CHECK(std::all_of(reply.begin(), reply.end(), [](u8 byte) { return byte == 0; }));
        }
        const auto before = fixture.bus.controller_paks;
        CHECK_EQ(fixture.command(port, {2, 0x80, 1}, 33), std::vector<u8>(33));
        std::vector<u8> input{3, 0x7f, static_cast<u8>(0xe0U | test::address_remainder(0x7fe0))};
        input.insert(input.end(), 40, u8{0x5a});
        fixture.packet(port, input, 1);
        CHECK_EQ(fixture.execute(1)[0], test::data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
        auto expected = before;
        std::fill_n(expected[port].begin() + 2 * 32768 + 0x7fe0, 32, u8{0x5a});
        CHECK_EQ(fixture.bus.controller_paks, expected);
    }
}

TEST(controller_pak_banks_console_channel_reset_and_input_updates_preserve_selection) {
    BankedFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        fixture.select(port, 2);
    const auto stored = fixture.bus.controller_paks;
    fixture.system.reset();
    for (unsigned port = 0; port < 4; ++port) {
        CHECK_EQ(fixture.tag(port), 0x23U + port);
        CHECK(fixture.bus.configure_controller_pak(port, 3));
        auto state = fixture.bus.controllers()[port];
        state.buttons = 0x8123;
        state.stick_x = 7;
        fixture.bus.set_controller_state(port, state);
        CHECK_EQ(fixture.status(port), 1U);
        CHECK_EQ(fixture.command(port, {0xff}, 3)[2], 1U);
        std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
        fixture.bus.pif[0x7c0 + port] = 0xfd;
        fixture.bus.pif[0x7c1 + port] = 0xfe;
        fixture.bus.joybus.configure();
        fixture.bus.joybus.execute();
        CHECK_EQ(fixture.tag(port), 0x23U + port);
    }
    CHECK_EQ(fixture.bus.controller_paks, stored);
}

TEST(controller_pak_banks_reconnect_and_resize_restore_bank_zero_and_detection) {
    for (unsigned transition = 0; transition < 4; ++transition) {
        BankedFixture fixture;
        const auto stored = fixture.bus.controller_paks;
        for (unsigned port = 0; port < 4; ++port) {
            fixture.select(port, 2);
            auto state = fixture.bus.controllers()[port];
            const auto attached = state;
            if (transition == 0)
                state.connected = false;
            else if (transition == 1)
                state.accessory = ControllerAccessory::None;
            else
                state.device = transition == 2 ? ControllerDevice::Mouse : ControllerDevice::GameCube;
            fixture.bus.set_controller_state(port, state);
            fixture.bus.set_controller_state(port, attached);
            fixture.packet(port, select_packet(2), 1);
            CHECK_EQ(fixture.execute(1)[0],
                     test::data_remainder(std::span<const u8, 32>(select_packet(2).data() + 3, 32)) ^ 0xffU);
            CHECK_EQ(fixture.status(port), 3U);
            CHECK_EQ(fixture.tag(port), 0x21U + port);
            CHECK_EQ(fixture.bus.controller_paks, stored);
        }
    }
    BankedFixture fixture;
    for (unsigned port = 0; port < 4; ++port) {
        fixture.select(port, 2);
        const auto before = fixture.bus.controller_paks[port];
        CHECK(fixture.bus.configure_controller_pak(port, 2));
        CHECK_EQ(fixture.status(port), 3U);
        CHECK_EQ(fixture.tag(port), 0x21U + port);
        CHECK_EQ(fixture.bus.controller_paks[port].size(), 65536U);
        CHECK(std::equal(fixture.bus.controller_paks[port].begin(), fixture.bus.controller_paks[port].end(),
                         before.begin()));
    }
}

TEST(controller_pak_banks_si_selection_commits_at_completion_for_every_clock_step) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool cpu_write : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true})
                    for (bool complete : {false, true}) {
                        BankedFixture fixture(2);
                        test::initialize_memory(fixture.system);
                        const auto stored = fixture.bus.controller_paks;
                        fixture.packet(port, select_packet(1), 1);
                        test::configure_joybus(fixture.bus, cpu_write);
                        fixture.bus.write(0x04800000, 4, 0x2000);
                        fixture.bus.write(0x04800004, 4, 0x1fc007c0);
                        const u64 cycles = 37020 + port * 1420;
                        const u64 deadline = cpu_clock ? (cycles * 3 + 1) / 2 : cycles;
                        const u64 elapsed = deadline - (complete ? 0U : 1U);
                        const auto advance = [&](u64 amount) {
                            if (cpu_clock)
                                fixture.system.advance(amount);
                            else
                                fixture.bus.tick(amount);
                        };
                        if (single) {
                            for (u64 index = 0; index < elapsed; ++index)
                                advance(1);
                        } else {
                            advance(elapsed);
                        }
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, complete ? 0x1000U : 1U);
                        fixture.system.reset();
                        CHECK_EQ(fixture.tag(port), 0x21U + port + (complete ? 1U : 0U));
                        CHECK_EQ(fixture.bus.controller_paks, stored);
                    }
}
