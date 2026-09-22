#include "controller_pak_fixture.hpp"

namespace {
using namespace cupid;
using test::PakFixture;

std::vector<u8> poll(PakFixture& fixture, unsigned port, u16 buttons, s8 x = -17, s8 y = 43) {
    ControllerState state;
    state.buttons = buttons;
    state.stick_x = x;
    state.stick_y = y;
    fixture.bus.set_controller_state(port, state);
    const auto reply = fixture.command(port, {1}, 4);
    CHECK_EQ(fixture.bus.pif[0x7c1 + port], 4U);
    return reply;
}
} // namespace

TEST(controller_poll_reserved_bits_are_not_host_buttons) {
    PakFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (u16 reserved : {u16{0}, u16{0x40}, u16{0x80}, u16{0xc0}}) {
            const auto reply = poll(fixture, port, static_cast<u16>(0x8021 | reserved));
            CHECK_EQ(reply[0], 0x80U);
            CHECK_EQ(reply[1], 0x21U);
            CHECK_EQ(reply[2], 0xefU);
            CHECK_EQ(reply[3], 43U);
        }
}

TEST(controller_poll_opposing_dpad_directions_cancel_on_each_axis) {
    PakFixture fixture;
    const std::array<u8, 16> expected{0, 1, 2, 0, 4, 5, 6, 4, 8, 9, 10, 8, 0, 1, 2, 0};
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned directions = 0; directions < 16; ++directions) {
            const auto reply = poll(fixture, port, static_cast<u16>(0xa02f | (directions << 8)));
            CHECK_EQ(reply[0], 0xa0U | expected[directions]);
            CHECK_EQ(reply[1], 0x2fU);
            CHECK_EQ(reply[2], 0xefU);
            CHECK_EQ(reply[3], 43U);
        }
}

TEST(controller_poll_stick_reset_chord_requires_all_three_buttons_and_releases_cleanly) {
    PakFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned combination = 0; combination < 8; ++combination) {
            const auto buttons =
                static_cast<u16>(0x800f | ((combination & 1) ? 0x20 : 0) | ((combination & 2) ? 0x10 : 0) |
                                 ((combination & 4) ? 0x1000 : 0));
            const auto reply = poll(fixture, port, buttons, -128, 127);
            CHECK_EQ(reply[0], combination == 7 ? 0x80U : buttons >> 8);
            CHECK_EQ(reply[1], combination == 7 ? 0xbfU : buttons & 0xffU);
            CHECK_EQ(reply[2], combination == 7 ? 0U : 0x80U);
            CHECK_EQ(reply[3], combination == 7 ? 0U : 127U);
            CHECK_EQ(fixture.bus.controllers()[port].buttons, buttons);
            CHECK_EQ(fixture.bus.controllers()[port].stick_x, -128);
            CHECK_EQ(fixture.bus.controllers()[port].stick_y, 127);
            const auto released = poll(fixture, port, 0x800f, -128, 127);
            CHECK_EQ(released[0], 0x80U);
            CHECK_EQ(released[1], 0x0fU);
            CHECK_EQ(released[2], 0x80U);
            CHECK_EQ(released[3], 127U);
        }
}

TEST(controller_poll_reset_chord_prefixes_preserve_packet_end_and_pak_detection) {
    PakFixture fixture;
    for (unsigned port = 0; port < 4; ++port)
        for (u8 receive = 0; receive <= 8; ++receive) {
            fixture.insert(port);
            ControllerState state;
            state.buttons = 0x1030;
            state.stick_x = -17;
            state.stick_y = 43;
            fixture.bus.set_controller_state(port, state);
            const auto reply = fixture.command(port, {1}, receive);
            const std::array<u8, 8> expected{0, 0xb0, 0, 0, 0, 0, 0, 0};
            CHECK(std::equal(reply.begin(), reply.end(), expected.begin()));
            CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive | (receive > 4 ? 0x40U : 0U));
            CHECK_EQ(fixture.bus.pif[fixture.response + receive], 0xfeU);
            CHECK_EQ(fixture.status(port), 3U);
        }
}
