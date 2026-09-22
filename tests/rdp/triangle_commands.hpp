#pragma once

#include "texture_commands.hpp"

namespace test::rdp {

struct TriangleGeometry {
    s32 top = 0, middle = 8, bottom = 8;
    s32 major = 0, upper = 0x40000, lower = 0x40000;
    s32 major_step = 0, upper_step = 0, lower_step = 0;
    bool left_major = true;
    unsigned tile = 0, maximum_level = 0;
};

struct TriangleCommandAttributes {
    std::array<u32, 4> value{}, dx{}, de{}, dy{};
};

struct TriangleCommands : TextureCommands {
    TriangleCommandAttributes shade{{0x00800000, 0x00400000, 0x00200000, 0x00ff0000}};
    TriangleCommandAttributes texture{
        {0, 0, 0x7fff0000, 0}, {0x00200000, 0, 0, 0}, {0, 0x00200000, 0, 0}, {0, 0x00200000, 0, 0}};
    std::array<u32, 4> z{0x40000000, 0, 0, 0};

    TriangleCommands() {
        append(0x3c, combine_word({}, {}));
        append(0x3e, 0x9000);
    }
    void data(u64 value) {
        system->bus.memory.write(end, 8, value);
        end += 8;
    }
    void group(const TriangleCommandAttributes& attributes) {
        for (const auto& pair :
             {std::array{attributes.value, attributes.dx}, std::array{attributes.de, attributes.dy}}) {
            for (unsigned part = 0; part < 2; ++part) {
                for (const auto& components : pair) {
                    u64 word = 0;
                    for (unsigned i = 0; i < 4; ++i)
                        word |= static_cast<u64>((components[i] >> (part == 0 ? 16U : 0U)) & 65535U)
                                << (48U - i * 16U);
                    data(word);
                }
            }
        }
    }
    void triangle(unsigned opcode = 8, TriangleGeometry geometry = {}) {
        append(opcode, (static_cast<u64>(geometry.left_major) << 55U) |
                           (static_cast<u64>(geometry.maximum_level) << 51U) |
                           (static_cast<u64>(geometry.tile) << 48U) |
                           (static_cast<u64>(static_cast<u32>(geometry.bottom) & 0x3fffU) << 32U) |
                           (static_cast<u64>(static_cast<u32>(geometry.middle) & 0x3fffU) << 16U) |
                           (static_cast<u32>(geometry.top) & 0x3fffU));
        for (const auto& edge : {std::array{geometry.lower, geometry.lower_step},
                                 std::array{geometry.major, geometry.major_step},
                                 std::array{geometry.upper, geometry.upper_step}})
            data((static_cast<u64>(static_cast<u32>(edge[0])) << 32U) | static_cast<u32>(edge[1]));
        if ((opcode & 4U) != 0)
            group(shade);
        if ((opcode & 2U) != 0)
            group(texture);
        if ((opcode & 1U) != 0) {
            data((static_cast<u64>(z[0]) << 32U) | z[1]);
            data((static_cast<u64>(z[2]) << 32U) | z[3]);
        }
    }
    u16 depth(unsigned x, unsigned y = 0) const {
        return static_cast<u16>(system->bus.memory.read(0x9000U + (y * 16U + x) * 2U, 2));
    }
};

} // namespace test::rdp
