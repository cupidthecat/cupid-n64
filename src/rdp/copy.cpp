#include "cupid/bus.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

s32 copy_coordinate(s32 value, unsigned shift, unsigned low) {
    if (shift <= 10U)
        value >>= shift;
    else
        value = std::bit_cast<s16>(static_cast<u16>(static_cast<u32>(value) << (16U - shift)));
    return (value - static_cast<s32>(low * 8U)) >> 5;
}

u32 masked_coordinate(s32 value, unsigned mask, bool mirror) {
    u32 coordinate = static_cast<u32>(value);
    if (mask != 0) {
        const u32 period = 1U << std::min(mask, 10U);
        if (mirror && (coordinate & period) != 0)
            coordinate ^= period - 1U;
        coordinate &= period - 1U;
    }
    return coordinate;
}

} // namespace

u16 Rdp::copy_texel(const RdpTile& tile, s32 s, s32 t, unsigned lane) const {
    s = copy_coordinate(s, tile.s_shift, tile.s_low);
    t = copy_coordinate(t, tile.t_shift, tile.t_low);
    const bool palette = (other_modes_ & (1ULL << 47U)) != 0;
    const unsigned word_lane = color_image_size_ == 1U ? lane / 2U : lane;
    const u32 row = masked_coordinate(t, tile.t_mask, tile.t_mirror);
    const unsigned index_mask = tile.size == 3U || palette ? 0x3ffU : 0x7ffU;
    const auto read_word = [&](unsigned index) {
        const unsigned address = (index & 0x7ffU) * 2U;
        return static_cast<u16>((static_cast<unsigned>(texture_memory_[address]) << 8U) |
                                texture_memory_[address + 1U]);
    };
    const auto nibble_address = [&](s32 column) {
        const u32 x = masked_coordinate(column, tile.s_mask, tile.s_mirror);
        const u32 base = static_cast<u32>(tile.tmem_address) + tile.line_stride * row;
        return ((base * 2U + (x << std::min<unsigned>(tile.size, 2U))) & 0x1fffU) ^ ((row & 1U) * 8U);
    };
    u16 word;
    if (word_lane < 2U && tile.size != 2U && !palette) {
        const auto byte = [&](unsigned offset) {
            const unsigned address = nibble_address(s + static_cast<s32>(word_lane * 2U + offset));
            const unsigned value = read_word((address >> 2U) & index_mask);
            if (tile.size == 0U)
                return ((value >> (12U - (address & 3U) * 4U)) & 15U) * 17U;
            if (tile.size == 1U)
                return (value >> (8U - (address & 2U) * 4U)) & 255U;
            return value >> 8U;
        };
        word = static_cast<u16>((byte(0) << 8U) | byte(1));
    } else {
        const unsigned address = nibble_address(s + static_cast<s32>(word_lane));
        word = read_word((address >> 2U) & index_mask);
        if (palette) {
            const unsigned entry = tile.size == 0U ? ((word >> (12U - (address & 3U) * 4U)) & 15U) |
                                                         (static_cast<unsigned>(tile.palette) << 4U)
                                                   : (word >> (8U - (address & 2U) * 4U)) & 255U;
            word = read_word(0x400U | (entry * 4U + word_lane));
        }
    }
    if (color_image_size_ == 1U)
        word = static_cast<u16>((word >> ((lane & 1U) == 0 ? 8U : 0U)) & 255U);
    return word;
}

void Rdp::copy_rectangle(u64 command, bool flipped) {
    if (((other_modes_ >> 52U) & 3U) != 2U || color_image_size_ == 3U ||
        (color_image_size_ == 2U && color_image_format_ != 0U))
        return;
    const unsigned raw_left = static_cast<unsigned>((command >> 12U) & 0xfffU);
    const unsigned raw_right = static_cast<unsigned>((command >> 44U) & 0xfffU);
    const unsigned raw_top = static_cast<unsigned>(command & 0xfffU);
    const unsigned raw_bottom = static_cast<unsigned>((command >> 32U) & 0xfffU);
    if (raw_left > raw_right || raw_left >= scissor_x1_ || raw_right < scissor_x0_)
        return;
    const unsigned y0 = std::max<unsigned>(raw_top, scissor_y0_);
    const unsigned y1 = std::min<unsigned>(raw_bottom | 3U, scissor_y1_);
    if (y0 >= y1)
        return;
    const unsigned left = std::min<unsigned>(std::max<unsigned>(raw_left, scissor_x0_), scissor_x1_) >> 2U;
    const unsigned right = std::min<unsigned>(std::max<unsigned>(raw_right, scissor_x0_), scissor_x1_) >> 2U;
    const bool untextured = ((command >> 56U) & 0x3fU) == 0x36U;
    const u64 attributes = untextured ? 0U : buffered_word(8);
    const u32 initial_s = static_cast<u32>(attributes >> 48U) << 16U;
    const u32 initial_t = static_cast<u32>((attributes >> 32U) & 0xffffU) << 16U;
    const u32 ds = static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(static_cast<u16>(attributes >> 16U))))
                   << 11U;
    const u32 dt = static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(static_cast<u16>(attributes))))
                   << 11U;
    const RdpTile& tile = tiles_[untextured ? 0U : (command >> 24U) & 7U];
    const unsigned group_size = color_image_size_ == 1U ? 8U : 4U;
    for (unsigned y = y0 >> 2U; y <= (y1 - 1U) >> 2U; ++y) {
        if (scissor_field_enabled_ && (y & 1U) != static_cast<unsigned>(scissor_keep_odd_))
            continue;
        const unsigned dy = y - (raw_top >> 2U);
        for (unsigned x = left; x <= right; ++x) {
            const unsigned group = (x - left) / group_size;
            // Accumulators wrap at 32 bits before extracting signed 10.5 coordinates.
            s32 s = std::bit_cast<s32>(initial_s + ds * (flipped ? dy : group)) >> 16;
            s32 t = std::bit_cast<s32>(initial_t + dt * (flipped ? group : dy)) >> 16;
            // Rectangle commands supply W=0, which saturates a perspective divide.
            if ((other_modes_ & (1ULL << 51U)) != 0)
                s = t = 0x7fff;
            const u16 value = color_image_size_ == 0U ? 0U : copy_texel(tile, s, t, (x - left) % group_size);
            write_copy_pixel(x, y, value);
        }
    }
}

void Rdp::write_copy_pixel(unsigned x, unsigned y, u16 value) {
    if (color_image_size_ == 2U && (other_modes_ & 1U) != 0 && (value & 1U) == 0)
        return;
    const unsigned bytes = color_image_size_ == 2U ? 2U : 1U;
    const u32 address = color_image_address_ + (y * color_image_width_ + x) * bytes;
    const u8 hidden = bus_.memory.hidden_pair(address);
    bus_.memory.write(address, bytes, value);
    if (bytes == 1U)
        bus_.memory.set_hidden_pair(address,
                                    (address & 1U) != 0 ? static_cast<u8>((value & 1U) * 3U) : hidden);
}

} // namespace cupid
