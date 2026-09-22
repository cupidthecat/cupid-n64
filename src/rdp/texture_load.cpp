#include "cupid/bus.hpp"

namespace cupid {

void Rdp::set_tile(u64 command) {
    auto& tile = tiles_[(command >> 24U) & 7U];
    tile.tmem_address = static_cast<u16>(((command >> 32U) & 0x1ffU) * 8U);
    tile.line_stride = static_cast<u16>(((command >> 41U) & 0x1ffU) * 8U);
    tile.size = static_cast<u8>((command >> 51U) & 3U);
    tile.format = static_cast<u8>((command >> 53U) & 7U);
    tile.palette = static_cast<u8>((command >> 20U) & 15U);
    tile.s_shift = static_cast<u8>(command & 15U);
    tile.s_mask = static_cast<u8>((command >> 4U) & 15U);
    tile.s_mirror = (command & (1ULL << 8U)) != 0;
    tile.s_clamp = (command & (1ULL << 9U)) != 0;
    tile.t_shift = static_cast<u8>((command >> 10U) & 15U);
    tile.t_mask = static_cast<u8>((command >> 14U) & 15U);
    tile.t_mirror = (command & (1ULL << 18U)) != 0;
    tile.t_clamp = (command & (1ULL << 19U)) != 0;
}

void Rdp::set_tile_size(u64 command) {
    auto& tile = tiles_[(command >> 24U) & 7U];
    tile.s_low = static_cast<u16>((command >> 44U) & 0xfffU);
    tile.t_low = static_cast<u16>((command >> 32U) & 0xfffU);
    tile.s_high = static_cast<u16>((command >> 12U) & 0xfffU);
    tile.t_high = static_cast<u16>(command & 0xfffU);
}

void Rdp::load_texture(u64 command, u8 opcode) {
    const bool block = opcode == 0x33;
    const bool palette = opcode == 0x30;
    const unsigned sl = static_cast<unsigned>((command >> 44U) & 0xfffU);
    const unsigned tl = static_cast<unsigned>((command >> 32U) & 0xfffU);
    const unsigned sh = static_cast<unsigned>((command >> 12U) & 0xfffU);
    const unsigned th = static_cast<unsigned>(command & 0xfffU);
    if (palette && (th >> 2U) > (tl >> 2U)) {
        halt_commands();
        return;
    }
    if (!block && (th >> 2U) < (tl >> 2U))
        return;
    const unsigned width = ((block ? sh - sl : (sh >> 2U) - (sl >> 2U)) + 1U) & 0xfffU;
    if (width == 0 || (block && width > 2048U))
        return;

    set_tile_size(command);
    const auto& tile = tiles_[(command >> 24U) & 7U];
    const bool yuv = tile.format == 1;
    if (yuv && (tile.size != 2 || texture_image_size_ != 2))
        return;
    if (texture_image_size_ == 0) {
        halt_commands();
        return;
    }
    if (tile.size == 3 && (tile.format != 0 || palette))
        return;
    // These mismatched transfer configurations have no implemented load path.
    if (!palette &&
        ((texture_image_size_ == 3 && tile.size < 2) || (texture_image_size_ == 2 && tile.size == 0)))
        return;

    const unsigned bytes_per_pixel = 1U << (texture_image_size_ - 1U);
    const unsigned pixels_per_word = 8U / bytes_per_pixel;
    const unsigned x0 = block ? sl : sl >> 2U;
    const unsigned y0 = block ? tl : tl >> 2U;
    const u32 source_base = texture_image_address_ + (y0 * texture_image_width_ + x0) * bytes_per_pixel;
    const auto read_byte = [&](u32 address) { return static_cast<u8>(bus_.memory.read(address, 1)); };
    const auto write_byte = [&](unsigned address, u8 value) { texture_memory_[address & 0xfffU] = value; };

    if (palette) {
        // A 16-bit source advances one entry per transfer. Other source sizes
        // fetch a full word and distribute its first halfword to four banks.
        const unsigned step = texture_image_size_ == 2 ? 1U : pixels_per_word;
        for (unsigned x = 0; x < width; x += step) {
            unsigned destination;
            if (texture_image_size_ == 1)
                destination = x << (tile.size + 1U);
            else if (texture_image_size_ == 2)
                destination = x << (tile.size + 3U);
            else
                destination = ((x << tile.size) >> 2U) * 8U;
            destination += tile.tmem_address;
            const u32 source = source_base + x * bytes_per_pixel;
            for (unsigned lane = 0; lane < 4; ++lane) {
                const u32 address = source + (source & 1U) * lane * 2U;
                write_byte(destination + lane * 2U, read_byte(address));
                write_byte(destination + lane * 2U + 1U, read_byte(address + 1U));
            }
        }
        return;
    }

    const unsigned last_t = block ? (((width - 1U) / pixels_per_word) * th) >> 11U : 0U;
    if (last_t != 0 && texture_image_size_ > tile.size)
        return;
    const bool split_banks = tile.size == 3 || yuv;
    const unsigned height = block ? 1U : (th >> 2U) - y0 + 1U;
    for (unsigned row = 0; row < height; ++row) {
        const u32 source_row = source_base + row * texture_image_width_ * bytes_per_pixel;
        for (unsigned x = 0; x < width; x += pixels_per_word) {
            const unsigned t = block ? ((x / pixels_per_word) * th) >> 11U : row;
            const unsigned row_base = tile.tmem_address + t * tile.line_stride;
            const u32 source = source_row + x * bytes_per_pixel;
            std::array<u8, 8> word{};
            for (unsigned byte = 0; byte < word.size(); ++byte)
                word[byte] = read_byte(source + byte);
            if (split_banks) {
                const unsigned destination = row_base + (yuv ? x : x * 2U);
                for (unsigned pair = 0; pair < 2; ++pair) {
                    for (unsigned byte = 0; byte < 2; ++byte) {
                        const unsigned address =
                            ((destination + pair * 2U + byte) ^ ((t & 1U) * 4U)) & 0x7ffU;
                        const unsigned low = yuv ? byte * 2U : byte;
                        const unsigned high = yuv ? byte * 2U + 1U : byte + 2U;
                        write_byte(address, word[pair * 4U + low]);
                        write_byte(address + 0x800U, word[pair * 4U + high]);
                    }
                }
            } else {
                const unsigned destination = row_base + (((x << tile.size) >> 1U) & ~7U);
                for (unsigned byte = 0; byte < word.size(); ++byte)
                    write_byte(destination + (byte ^ ((t & 1U) * 4U)), word[byte]);
            }
        }
    }
}

} // namespace cupid
