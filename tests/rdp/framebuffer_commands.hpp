#pragma once

#include "triangle_commands.hpp"

namespace test::rdp {

struct FramebufferCommands : TriangleCommands {
    unsigned pixel_size, format, width;
    u32 base;

    explicit FramebufferCommands(unsigned image_size, unsigned image_format = 0, unsigned image_width = 5,
                                 u32 address = 0x8000)
        : pixel_size(image_size), format(image_format), width(image_width), base(address) {
        append(0x3f, (static_cast<u64>(format) << 53U) | (static_cast<u64>(pixel_size) << 51U) |
                         (static_cast<u64>(width - 1U) << 32U) | base);
        append(0x3a, 0x814325ff);
        for (unsigned offset = 0; offset < 128; ++offset)
            system->bus.memory.write((base & ~3U) + offset, 1, 0xa5);
        for (unsigned offset = 0; offset < 128; offset += 2)
            system->bus.memory.set_hidden_pair((base & ~3U) + offset, 2);
    }
    unsigned bytes() const {
        return pixel_size < 2U ? 1U : 1U << (pixel_size - 1U);
    }
    u32 address(unsigned x, unsigned y = 0) const {
        return (base & ~(bytes() - 1U)) + (y * width + x) * bytes();
    }
    u32 pixel(unsigned x = 0, unsigned y = 0) const {
        return static_cast<u32>(system->bus.memory.read(address(x, y), bytes()));
    }
    u32 solid(unsigned x = 0, unsigned y = 0, unsigned coverage = 7) const {
        if (pixel_size == 0U)
            return 0;
        if (pixel_size == 1U)
            return (address(x, y) & 1U) != 0 ? 0x43U : 0x81U;
        if (pixel_size == 2U)
            return format == 0U ? 0x8208U | (coverage >> 2U) : 0x8100U | (coverage << 5U);
        return 0x81432500U | (coverage << 5U);
    }
};

} // namespace test::rdp
