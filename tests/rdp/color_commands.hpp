#pragma once

#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <memory>

namespace test::rdp {
using namespace cupid;

struct CombineCycle {
    unsigned a = 8, b = 8, c = 16, d = 3;
    unsigned aa = 7, ab = 7, ac = 7, ad = 3;
};

inline u64 combine_word(CombineCycle first, CombineCycle second) {
    return (static_cast<u64>(first.a) << 52U) | (static_cast<u64>(first.b) << 28U) |
           (static_cast<u64>(first.c) << 47U) | (static_cast<u64>(first.d) << 15U) |
           (static_cast<u64>(first.aa) << 44U) | (static_cast<u64>(first.ab) << 12U) |
           (static_cast<u64>(first.ac) << 41U) | (static_cast<u64>(first.ad) << 9U) |
           (static_cast<u64>(second.a) << 37U) | (static_cast<u64>(second.b) << 24U) |
           (static_cast<u64>(second.c) << 32U) | (static_cast<u64>(second.d) << 6U) |
           (static_cast<u64>(second.aa) << 21U) | (static_cast<u64>(second.ab) << 3U) |
           (static_cast<u64>(second.ac) << 18U) | second.ad;
}

struct ColorCommands {
    std::unique_ptr<System> system = std::make_unique<System>();
    u32 end = 0x1000;
    unsigned size;

    explicit ColorCommands(unsigned pixel_size = 3) : size(pixel_size) {
        initialize_memory(*system);
        append(0x3f, (static_cast<u64>(size) << 51U) | (7ULL << 32U) | 0x8000U);
        modes();
        append(0x3c, combine_word({}, {}));
        append(0x3a, 0x804020ff);
    }
    void append(unsigned opcode, u64 payload) {
        system->bus.write(end, 8, (static_cast<u64>(opcode) << 56U) | payload);
        end += 8;
    }
    void modes(u64 bits = 0) {
        append(0x2f, (15ULL << 36U) | bits);
    }
    void rectangle(unsigned left = 0, unsigned top = 0, unsigned right = 4, unsigned bottom = 4) {
        append(0x36, (static_cast<u64>(right) << 44U) | (static_cast<u64>(bottom) << 32U) |
                         (static_cast<u64>(left) << 12U) | top);
    }
    void run() {
        append(0x29, 0);
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
        CHECK_EQ(system->bus.rdp.current(), end);
        CHECK_EQ(system->bus.rdp.read_register(12) & 0x62U, 0U);
    }
    u32 address(unsigned x, unsigned y = 0) const {
        return 0x8000U + (y * 8U + x) * (size == 2U ? 2U : 4U);
    }
    u32 pixel(unsigned x = 0, unsigned y = 0) const {
        return static_cast<u32>(system->bus.memory.read(address(x, y), size == 2U ? 2U : 4U));
    }
};
} // namespace test::rdp
