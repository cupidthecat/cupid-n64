#pragma once

#include "cupid/system.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace test {
using namespace cupid;

struct PakFixture {
    System system;
    Bus& bus{system.bus};
    unsigned response{};

    void packet(unsigned port, std::span<const u8> input, u8 receive, u8 flags = 0) {
        CHECK(port < 4);
        CHECK(port + 2 + input.size() + receive < 63);
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
} // namespace test
