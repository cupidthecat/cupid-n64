#pragma once

#include "cupid/bus.hpp"
#include "test.hpp"

#include <algorithm>

namespace test {

inline void configure_joybus(cupid::Bus& bus, bool cpu_write = true) {
    using namespace cupid;
    bus.pif[0x7ff] = 1;
    auto expected = bus.pif;
    expected[0x7ff] = 0;
    if (cpu_write) {
        bus.write(0x1fc007fc, 4, read_be32(bus.pif.data() + 0x7fc));
        static_cast<void>(bus.read(0x1fc007fc, 4));
    } else {
        for (u32 index = 0; index < 64; ++index)
            bus.write_ram_byte(0x2000 + index, bus.pif[0x7c0 + index]);
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        bus.write(0x04800000, 4, 0x2000);
        bus.write(0x04800010, 4, 0x1fc007c0);
        bus.tick(4064);
        CHECK_EQ(bus.pif[0x7ff], 0U);
        CHECK_EQ(bus.read(0x04800018, 4) & 1U, 1U);
        bus.tick(1);
        CHECK_EQ(bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
        bus.write(0x04800018, 4, 0);
    }
    CHECK(bus.pif == expected);
}

} // namespace test
