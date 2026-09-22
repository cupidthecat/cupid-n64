#pragma once

#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <memory>

namespace test::rdp {
using namespace cupid;

struct CopyCommands {
    std::unique_ptr<System> system = std::make_unique<System>();
    u32 end = 0x1000;
    unsigned destination_size;

    explicit CopyCommands(unsigned size = 2, unsigned format = 0) : destination_size(size) {
        test::initialize_memory(*system);
        append(0x3f, (static_cast<u64>(format) << 53U) | (static_cast<u64>(size) << 51U) | (15ULL << 32U) |
                         0x8000U);
        modes(0);
        for (unsigned i = 0; i < 256; ++i)
            system->bus.memory.write(0x10000U + i * 2U, 2, 0x1001U + i * 2U);
        append(0x3d, (2ULL << 51U) | (15ULL << 32U) | 0x10000U);
        tile(2);
        append(0x34, (60ULL << 12U) | 60U);
    }

    void append(unsigned opcode, u64 payload) {
        system->bus.write(end, 8, (static_cast<u64>(opcode) << 56U) | payload);
        end += 8;
    }
    void modes(u64 bits) {
        append(0x2f, (2ULL << 52U) | bits);
    }
    void tile(unsigned size, u64 bits = 0, unsigned index = 0, unsigned offset = 0) {
        append(0x35, (static_cast<u64>(size) << 51U) | (4ULL << 41U) | (static_cast<u64>(offset) << 32U) |
                         (static_cast<u64>(index) << 24U) | bits);
    }
    void bounds(unsigned s, unsigned t, unsigned index = 0) {
        append(0x32, (static_cast<u64>(s) << 44U) | (static_cast<u64>(t) << 32U) |
                         (static_cast<u64>(index) << 24U));
    }
    void scissor(unsigned left, unsigned top, unsigned right, unsigned bottom, unsigned field = 0) {
        append(0x2d, (static_cast<u64>(left) << 44U) | (static_cast<u64>(top) << 32U) |
                         (static_cast<u64>(right) << 12U) | bottom | (static_cast<u64>(field) << 24U));
    }
    void rectangle(unsigned left = 0, unsigned top = 0, unsigned right = 28, unsigned bottom = 4, s16 s = 0,
                   s16 t = 0, s16 ds = 4096, s16 dt = 1024, bool flip = false, unsigned index = 0) {
        append(flip ? 0x25U : 0x24U, (static_cast<u64>(right) << 44U) | (static_cast<u64>(bottom) << 32U) |
                                         (static_cast<u64>(index) << 24U) | (static_cast<u64>(left) << 12U) |
                                         top);
        system->bus.write(end, 8,
                          (static_cast<u64>(static_cast<u16>(s)) << 48U) |
                              (static_cast<u64>(static_cast<u16>(t)) << 32U) |
                              (static_cast<u64>(static_cast<u16>(ds)) << 16U) | static_cast<u16>(dt));
        end += 8;
    }
    void run() {
        append(0x29, 0);
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
        CHECK_EQ(system->bus.rdp.current(), end);
        CHECK_EQ(system->bus.rdp.read_register(12) & 0x62U, 0U);
    }
    u64 pixel(unsigned x, unsigned y = 0) const {
        const unsigned bytes = destination_size == 2 ? 2U : 1U;
        return system->bus.memory.read(0x8000U + (y * 16U + x) * bytes, bytes);
    }
};
} // namespace test::rdp
