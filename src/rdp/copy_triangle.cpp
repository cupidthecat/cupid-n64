#include "cupid/rdp.hpp"

#include <bit>

namespace cupid {

RdpTextureAttributes Rdp::triangle_texture_attributes() const {
    RdpTextureAttributes attributes;
    const unsigned opcode = static_cast<unsigned>((buffered_word(0) >> 56U) & 0x3fU);
    if ((opcode & 2U) == 0)
        return attributes;
    const unsigned offset = (opcode & 4U) != 0 ? 96U : 32U;
    const auto decode = [&](unsigned start) {
        const u64 integers = buffered_word(offset + start);
        const u64 fractions = buffered_word(offset + start + 16U);
        std::array<u32, 3> values{};
        for (unsigned i = 0; i < values.size(); ++i) {
            const unsigned shift = 48U - i * 16U;
            values[i] = static_cast<u32>(((integers >> shift) & 0xffffU) << 16U) |
                        static_cast<u32>((fractions >> shift) & 0xffffU);
        }
        return values;
    };
    attributes.value = decode(0);
    attributes.dx = decode(8);
    attributes.de = decode(32);
    attributes.dy = decode(40);
    return attributes;
}

void Rdp::copy_triangle_span(unsigned y, unsigned left, unsigned right,
                             const RdpTextureAttributes& attributes) {
    if (color_image_size_ == 2U && color_image_format_ != 0U)
        return;
    const u64 command = buffered_word(0);
    const u32 top_bits = static_cast<u32>(command) & 0x3fffU;
    const s32 top = static_cast<s32>(top_bits ^ 0x2000U) - 0x2000;
    const u32 rows = static_cast<u32>(static_cast<s32>(y) - (top >> 2));
    const bool left_major = (command & (1ULL << 55U)) != 0;
    const bool negative_slope = (buffered_word(16) & 0x80000000U) != 0;
    const bool offset_latch = left_major == negative_slope;
    const RdpTile& tile = tiles_[(command >> 48U) & 7U];
    std::array<u32, 3> base{};
    for (unsigned i = 0; i < base.size(); ++i) {
        u32 offset = 0;
        if (offset_latch) {
            const u32 edge = attributes.de[i] & ~0x1ffU;
            const u32 vertical = attributes.dy[i] & ~0x1ffU;
            offset = edge - static_cast<u32>(std::bit_cast<s32>(edge) >> 2) - vertical +
                     static_cast<u32>(std::bit_cast<s32>(vertical) >> 2);
        }
        base[i] = (((attributes.value[i] + attributes.de[i] * rows) & ~0x1ffU) + offset) & ~0x3ffU;
    }
    const unsigned group_size = color_image_size_ == 1U ? 8U : 4U;
    for (unsigned x = left; x <= right; ++x) {
        const unsigned distance = left_major ? x - left : right - x;
        const u32 group = distance / group_size;
        const u32 advance = left_major ? group : 0U - group;
        std::array<s16, 3> coordinates{};
        for (unsigned i = 0; i < coordinates.size(); ++i) {
            const u32 value = base[i] + (attributes.dx[i] & ~0x1fU) * advance;
            coordinates[i] = std::bit_cast<s16>(static_cast<u16>(value >> 16U));
        }
        if ((other_modes_ & (1ULL << 51U)) != 0) {
            coordinates[0] = rdp_perspective_coordinate(coordinates[0], coordinates[2]);
            coordinates[1] = rdp_perspective_coordinate(coordinates[1], coordinates[2]);
        }
        const u16 value = color_image_size_ == 0U
                              ? 0U
                              : copy_texel(tile, coordinates[0], coordinates[1], distance % group_size);
        write_copy_pixel(x, y, value);
    }
}

} // namespace cupid
