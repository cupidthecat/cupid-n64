#include "cupid/rdp/texture_sampling.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

s32 signed_nine(s32 value) {
    return static_cast<s32>((static_cast<u32>(value) & 511U) ^ 256U) - 256;
}

RdpColor convert_color(const RdpColor& color, const std::array<u16, 6>& convert) {
    std::array<s32, 4> factors{};
    for (unsigned i = 0; i < 4; ++i)
        factors[i] = signed_nine(convert[i]) * 2 + 1;
    const s32 u = signed_nine(color[0]);
    const s32 v = signed_nine(color[1]);
    const s32 y = signed_nine(color[2]);
    return {y + ((factors[0] * v + 128) >> 8), y + ((factors[1] * u + factors[2] * v + 128) >> 8),
            y + ((factors[3] * u + 128) >> 8), y};
}

s32 coordinate(s32 value, unsigned low, unsigned high, unsigned shift, bool clamp) {
    value = std::clamp(value, -32768, 32767);
    if (shift <= 10U)
        value >>= shift;
    else
        value = std::bit_cast<s16>(static_cast<u16>(static_cast<u32>(value) << (16U - shift)));
    if (clamp && (value >> 3) >= static_cast<s32>(high))
        return static_cast<s32>(((high / 4U - low / 4U) & 1023U) * 32U);
    value -= static_cast<s32>(low * 8U);
    return clamp ? std::max(value, 0) : value;
}

s32 masked(s32 value, unsigned mask, bool mirror) {
    if (mask == 0)
        return value;
    const unsigned period = 1U << std::min(mask, 10U);
    unsigned raw = static_cast<unsigned>(value);
    if (mirror && (raw & period) != 0)
        raw ^= period - 1U;
    return static_cast<s32>(raw & (period - 1U));
}

RdpColor rgba16(unsigned value) {
    RdpColor result{};
    for (unsigned channel = 0; channel < 3; ++channel) {
        const unsigned component = (value >> (11U - channel * 5U)) & 31U;
        result[channel] = static_cast<s32>((component << 3U) | (component >> 2U));
    }
    result[3] = static_cast<s32>((value & 1U) * 255U);
    return result;
}

RdpColor texel_rgba16(const std::array<u8, 4096>& memory, const RdpTile& tile, s32 s, s32 t) {
    const unsigned x = static_cast<unsigned>(s);
    const unsigned y = static_cast<unsigned>(t);
    const unsigned row = tile.tmem_address + tile.line_stride * y;
    const unsigned swap = (y & 1U) * 4U;
    const unsigned address = ((row + x * 2U) ^ swap) & 4094U;
    const unsigned value = (static_cast<unsigned>(memory[address]) << 8U) | memory[address + 1U];
    return rgba16(value);
}

RdpColor ia16(unsigned value) {
    const s32 intensity = static_cast<s32>(value >> 8U);
    return {intensity, intensity, intensity, static_cast<s32>(value & 255U)};
}

RdpColor texel(const std::array<u8, 4096>& memory, const RdpTile& tile, s32 s, s32 t, s32 chroma_s,
               bool palette, bool palette_ia, unsigned bank) {
    const unsigned x = static_cast<unsigned>(s);
    const unsigned y = static_cast<unsigned>(t);
    const unsigned row = tile.tmem_address + tile.line_stride * y;
    const unsigned swap = (y & 1U) * 4U;
    const auto word = [&](unsigned address) {
        address &= 4094U;
        return (static_cast<unsigned>(memory[address]) << 8U) | memory[address + 1U];
    };
    if (tile.format > 4U || (tile.format == 1U && palette))
        return {};
    if (tile.format == 1U) {
        const unsigned uv = word(((row + static_cast<unsigned>(chroma_s) * 2U) & 2047U) ^ swap);
        const s32 luma = memory[(((row + x) & 2047U) ^ swap) | 2048U];
        return {static_cast<s32>(uv >> 8U) - 128, static_cast<s32>(uv & 255U) - 128, luma, luma};
    }
    const unsigned mask = palette || (tile.format == 0U && tile.size == 3U) ? 2047U : 4095U;
    const unsigned offset = tile.size == 0U ? x >> 1U : tile.size == 1U ? x : x * 2U;
    const unsigned address = ((row + offset) & mask) ^ swap;
    unsigned value = tile.size < 2U ? memory[address] : word(address);
    if (tile.size == 0U)
        value = (value >> ((~x & 1U) * 4U)) & 15U;
    if (palette) {
        const unsigned entry = tile.size == 0U   ? value | (static_cast<unsigned>(tile.palette) << 4U)
                               : tile.size == 1U ? value
                                                 : value >> 8U;
        value = word(2048U | (entry * 8U + bank * 2U));
        return palette_ia ? ia16(value) : rgba16(value);
    }
    if (tile.size == 0U) {
        if (tile.format == 3U) {
            const unsigned bits = value & 14U;
            const s32 intensity = static_cast<s32>((bits << 4U) | (bits << 1U) | (bits >> 2U));
            return {intensity, intensity, intensity, static_cast<s32>((value & 1U) * 255U)};
        }
        value = tile.format == 2U ? value | (static_cast<unsigned>(tile.palette) << 4U) : value * 17U;
    } else if (tile.size == 1U && tile.format == 3U) {
        const s32 intensity = static_cast<s32>((value >> 4U) * 17U);
        return {intensity, intensity, intensity, static_cast<s32>((value & 15U) * 17U)};
    } else if (tile.size == 2U) {
        if (tile.format == 3U)
            return ia16(value);
    } else if (tile.size == 3U && tile.format == 0U) {
        return rdp_unpack_color((value << 16U) | word(address | 2048U));
    }
    if (tile.size >= 2U)
        return {static_cast<s32>(value >> 8U), static_cast<s32>(value & 255U), static_cast<s32>(value >> 8U),
                static_cast<s32>(value & 255U)};
    const s32 intensity = static_cast<s32>(value);
    return {intensity, intensity, intensity, intensity};
}

template <typename Fetch>
void fetch_taps(std::array<RdpColor, 4>& taps, unsigned needed, const Fetch& fetch) {
    if ((needed & 1U) != 0)
        taps[0] = fetch(0);
    if ((needed & 6U) != 0) {
        taps[1] = fetch(1);
        taps[2] = fetch(2);
    }
    if ((needed & 8U) != 0)
        taps[3] = fetch(3);
}

} // namespace

RdpColor rdp_sample_texture(const std::array<u8, 4096>& memory, const RdpTile& tile, RdpTexturePoint point,
                            u64 modes, unsigned cycle, const std::array<u16, 6>& convert,
                            const RdpColor& previous) {
    const bool palette = (modes & (1ULL << 47U)) != 0;
    const bool palette_ia = (modes & (1ULL << 46U)) != 0;
    const bool quad = (modes & (1ULL << 45U)) != 0;
    const bool mid_enabled = (modes & (1ULL << 44U)) != 0;
    const bool filter = (modes & (1ULL << (43U - cycle))) != 0;
    const bool convert_previous = cycle == 1U && (modes & (1ULL << 41U)) != 0;
    if (convert_previous && !filter)
        return convert_color(previous, convert);
    if (convert_previous && !quad) {
        const s32 blue = signed_nine(previous[2]);
        return {blue, blue, blue, blue};
    }
    const s32 s =
        coordinate(point[0], tile.s_low, tile.s_high, tile.s_shift, tile.s_clamp || tile.s_mask == 0U);
    const s32 t =
        coordinate(point[1], tile.t_low, tile.t_high, tile.t_shift, tile.t_clamp || tile.t_mask == 0U);
    const s32 sf = quad || palette ? s & 31 : 0;
    const s32 tf = quad || palette ? t & 31 : 0;
    s32 s0 = masked(s >> 5, tile.s_mask, tile.s_mirror);
    s32 s1 = masked((s >> 5) + 1, tile.s_mask, tile.s_mirror);
    s32 t0 = masked(t >> 5, tile.t_mask, tile.t_mirror);
    s32 t1 = masked((t >> 5) + 1, tile.t_mask, tile.t_mirror);
    t1 = (t0 & 255) + std::max(t1 - t0, -255);
    t0 &= 255;
    const s32 chroma_fraction = ((s0 & 1) * 16) | (sf >> 1);
    const bool yuv = tile.format == 1U;
    if (palette && !quad) {
        s1 = s0;
        t1 = t0;
    }
    const unsigned bank_xor = palette && sf + tf >= 32 ? 3U : 0U;
    const std::array<s32, 4> xs = {s0, s1, s0, s1};
    const std::array<s32, 4> ys = {t0, t0, t1, t1};
    const s32 chroma0 = s0 >> 1;
    const s32 chroma1 = (2 * s1 - s0) >> 1;
    const auto required_taps = [&](s32 fraction) -> unsigned {
        if (yuv && !quad)
            return 1U;
        if (mid_enabled && filter && fraction == 16 && tf == 16)
            return 15U;
        const unsigned base = fraction + tf >= 32 ? 8U : 1U;
        return base | (convert_previous || (filter && (quad || palette)) ? 6U : 0U);
    };
    const unsigned needed = required_taps(sf) | (yuv ? required_taps(chroma_fraction) : 0U);
    std::array<RdpColor, 4> taps{};
    if (!palette && tile.format == 0U && tile.size == 2U) {
        fetch_taps(taps, needed,
                   [&](unsigned index) { return texel_rgba16(memory, tile, xs[index], ys[index]); });
    } else {
        fetch_taps(taps, needed, [&](unsigned index) {
            return texel(memory, tile, xs[index], ys[index], (index & 1U) != 0 ? chroma1 : chroma0, palette,
                         palette_ia, index ^ bank_xor);
        });
    }
    RdpColor result{};
    for (unsigned channel = 0; channel < 4; ++channel) {
        const s32 fraction = yuv && channel < 2U ? chroma_fraction : sf;
        const bool mid = mid_enabled && filter && fraction == 16 && tf == 16;
        const bool upper = fraction + tf >= 32 && !mid;
        const s32 base = taps[upper ? 3U : 0U][channel];
        if (convert_previous) {
            const s32 red = signed_nine(previous[0]);
            const s32 green = signed_nine(previous[1]);
            const s32 blue = signed_nine(previous[2]);
            if (mid)
                result[channel] = blue + ((red * (taps[2][channel] - taps[3][channel]) +
                                           green * (taps[1][channel] - taps[3][channel]) +
                                           64 * (taps[0][channel] - taps[3][channel]) + 128) >>
                                          8);
            else
                result[channel] = blue + (((upper ? green : red) * (taps[1][channel] - base) +
                                           (upper ? red : green) * (taps[2][channel] - base) + 128) >>
                                          8);
        } else if (yuv && !quad) {
            result[channel] = taps[0][channel];
        } else if (mid) {
            result[channel] =
                (taps[0][channel] + taps[1][channel] + taps[2][channel] + taps[3][channel] + 2) >> 2;
        } else if (filter && (quad || palette)) {
            const s32 first = upper ? 32 - tf : fraction;
            const s32 second = upper ? 32 - fraction : tf;
            result[channel] =
                base + (((taps[1][channel] - base) * first + (taps[2][channel] - base) * second + 16) >> 5);
        } else {
            result[channel] = base;
        }
    }
    return !filter && !convert_previous ? convert_color(result, convert) : result;
}

} // namespace cupid
