#pragma once

#include "color_commands.hpp"

namespace test::rdp {

struct TextureCommands : ColorCommands {
    TextureCommands() {
        append(0x3f, (3ULL << 51U) | (15ULL << 32U) | 0x8000U);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 16; ++x)
                system->bus.memory.write(0x10000U + (y * 16U + x) * 4U, 4, source_color(x, y));
        append(0x3d, (3ULL << 51U) | (15ULL << 32U) | 0x10000U);
        tile();
        append(0x34, (60ULL << 12U) | 12U);
        append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
        modes();
    }
    static u32 source_color(unsigned x, unsigned y) {
        return ((x + 1U) * 8U << 24U) | ((y + 1U) * 32U << 16U) | 0x4080U;
    }
    static u32 expected(unsigned x, unsigned y, unsigned coverage = 7) {
        return (source_color(x, y) & 0xffffff00U) | (coverage << 5U);
    }
    void modes(u64 bits = 0) {
        ColorCommands::modes((1ULL << 43U) | (1ULL << 42U) | bits);
    }
    void tile(unsigned index = 0, unsigned offset = 0, u64 flags = 0) {
        append(0x35, (3ULL << 51U) | (4ULL << 41U) | (static_cast<u64>(offset / 8U) << 32U) |
                         (static_cast<u64>(index) << 24U) | flags);
    }
    void second_tile() {
        tile(1, 64);
        append(0x32, (1ULL << 24U) | (60ULL << 12U));
    }
    void textured(unsigned left = 0, unsigned top = 0, unsigned right = 16, unsigned bottom = 8, s16 s = 0,
                  s16 t = 0, s16 ds = 1024, s16 dt = 1024, bool flip = false, unsigned index = 0) {
        append(flip ? 0x25U : 0x24U, (static_cast<u64>(right) << 44U) | (static_cast<u64>(bottom) << 32U) |
                                         (static_cast<u64>(index) << 24U) | (static_cast<u64>(left) << 12U) |
                                         top);
        system->bus.write(end, 8,
                          (static_cast<u64>(static_cast<u16>(s)) << 48U) |
                              (static_cast<u64>(static_cast<u16>(t)) << 32U) |
                              (static_cast<u64>(static_cast<u16>(ds)) << 16U) | static_cast<u16>(dt));
        end += 8;
    }
    u32 address(unsigned x, unsigned y = 0) const {
        return 0x8000U + (y * 16U + x) * 4U;
    }
    u32 pixel(unsigned x = 0, unsigned y = 0) const {
        return static_cast<u32>(system->bus.memory.read(address(x, y), 4));
    }
};
} // namespace test::rdp
