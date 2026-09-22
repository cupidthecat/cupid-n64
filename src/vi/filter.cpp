#include "cupid/vi/filter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <limits>

namespace cupid {
namespace {

ViColor divot(const ViPixel& left, const ViPixel& center, const ViPixel& right) {
    if ((left.coverage & center.coverage & right.coverage) == 7)
        return center.color;
    ViColor result{};
    for (unsigned channel = 0; channel < 3; ++channel) {
        const auto low = std::min(left.color[channel], right.color[channel]);
        const auto high = std::max(left.color[channel], right.color[channel]);
        result[channel] = std::clamp(center.color[channel], low, high);
    }
    return result;
}

template <class ReadPixel> ViPixel reconstruct_pixel(u32 control, bool repeat_lower, ReadPixel&& read) {
    ViPixel center = read(0, 0);
    if (center.coverage != 7) {
        const std::array<ViPixel, 6> neighbors = {
            read(-1, -1),
            read(1, -1),
            read(-2, 0),
            read(2, 0),
            repeat_lower ? read(-2, 0) : read(-1, 1),
            repeat_lower ? read(2, 0) : read(1, 1),
        };
        for (unsigned channel = 0; channel < 3; ++channel) {
            u32 low = center.color[channel];
            u32 high = low;
            u32 second_low = low;
            u32 second_high = low;
            for (const auto& neighbor : neighbors) {
                if (neighbor.coverage != 7)
                    continue;
                const u32 value = neighbor.color[channel];
                second_low = std::min(second_low, std::max(value, low));
                second_high = std::max(second_high, std::min(value, high));
                low = std::min(low, value);
                high = std::max(high, value);
            }
            const s32 correction =
                static_cast<s32>(second_low + second_high) - 2 * static_cast<s32>(center.color[channel]);
            const s32 delta = (correction * static_cast<s32>(7 - center.coverage) + 4) >> 3;
            center.color[channel] = static_cast<u32>(static_cast<s32>(center.color[channel]) + delta) & 255U;
        }
    } else if ((control & 0x10000U) != 0) {
        const ViColor original = center.color;
        std::array<s32, 3> adjustment{};
        const auto accumulate = [&](s32 dx, s32 dy) {
            const auto neighbor = read(dx, dy);
            for (unsigned channel = 0; channel < 3; ++channel) {
                const s32 difference =
                    static_cast<s32>(neighbor.color[channel] >> 3) - static_cast<s32>(original[channel] >> 3);
                adjustment[channel] += std::clamp<s32>(difference, -1, 1);
            }
        };
        for (s32 dx = -1; dx <= 1; ++dx) {
            accumulate(dx, -1);
            if (dx != 0)
                accumulate(dx, 0);
            if (!repeat_lower)
                accumulate(dx, 1);
        }
        if (repeat_lower) {
            accumulate(-1, 0);
            accumulate(1, 0);
        }
        for (unsigned channel = 0; channel < 3; ++channel)
            center.color[channel] = static_cast<u32>(
                std::clamp<s32>(static_cast<s32>(original[channel] & 248U) + adjustment[channel], 0, 255));
    }
    return center;
}

} // namespace

ViFilter::ViFilter(std::span<const u8> bytes, std::span<const u8> hidden, std::span<const u32, 14> registers)
    : bytes_(bytes), hidden_(hidden), control_(registers[0]), origin_(registers[1]), stride_(registers[2]),
      pixel_bytes_((registers[0] & 3U) == 3 ? 4 : 2), address_mask_(bytes.size() - 1),
      power_of_two_size_(std::has_single_bit(bytes.size())) {}

ViPixel ViFilter::fetch(s32 x, s32 y) const {
    if (bytes_.empty())
        return {};
    const s64 offset =
        static_cast<s64>(origin_ & ~(pixel_bytes_ - 1U)) + (static_cast<s64>(y) * stride_ + x) * pixel_bytes_;
    std::size_t address;
    if (power_of_two_size_) {
        address = static_cast<std::size_t>(offset) & address_mask_;
    } else {
        const s64 size = static_cast<s64>(bytes_.size());
        const s64 remainder = offset % size;
        address = static_cast<std::size_t>(remainder < 0 ? remainder + size : remainder);
    }
    u32 word = 0;
    if (bytes_.size() - address >= pixel_bytes_) {
        const auto* data = bytes_.data() + address;
        word = pixel_bytes_ == 2 ? (static_cast<u32>(data[0]) << 8) | data[1] : read_be32(data);
    } else {
        auto next = address;
        for (unsigned byte = 0; byte < pixel_bytes_; ++byte) {
            word = (word << 8) | bytes_[next];
            if (++next == bytes_.size())
                next = 0;
        }
    }
    ViPixel pixel;
    if (pixel_bytes_ == 2) {
        pixel.color = {(word >> 8) & 248U, (word >> 3) & 248U, (word << 2) & 248U};
        const u32 hidden = address / 2 < hidden_.size() ? hidden_[address / 2] & 3U : 0;
        pixel.coverage = ((word & 1U) << 2) | hidden;
    } else {
        pixel.color = {word >> 24, (word >> 16) & 255U, (word >> 8) & 255U};
        pixel.coverage = (word >> 5) & 7U;
    }
    if (((control_ >> 8) & 3U) >= 2)
        pixel.coverage = 7;
    return pixel;
}

ViPixel ViFilter::reconstruct(s32 x, s32 y, bool repeat_lower) const {
    return reconstruct_pixel(control_, repeat_lower, [&](s32 dx, s32 dy) { return fetch(x + dx, y + dy); });
}

ViColor ViFilter::sample(s32 x, s32 y, bool repeat_lower) const {
    const auto center = reconstruct(x, y, repeat_lower);
    if ((control_ & 16U) == 0)
        return center.color;
    const auto left = reconstruct(x - 1, y, repeat_lower);
    const auto right = reconstruct(x + 1, y, repeat_lower);
    return divot(left, center, right);
}

void ViFilter::sample_row(s32 x, s32 y, bool repeat_lower, std::span<ViColor> output) const {
    if (output.empty())
        return;
    constexpr std::size_t tile_width = 128;
    constexpr std::size_t halo = 3;
    constexpr std::size_t cached_width = tile_width + halo * 2;
    constexpr std::size_t minimum_cached_row = 32;
    const bool halo_fits =
        x >= std::numeric_limits<s32>::min() + static_cast<s32>(halo) &&
        static_cast<s64>(std::numeric_limits<s32>::max()) - x - 2 >= static_cast<s64>(output.size()) &&
        y > std::numeric_limits<s32>::min() && (repeat_lower || y < std::numeric_limits<s32>::max());
    if ((control_ & 0x10000U) != 0 && output.size() >= minimum_cached_row && halo_fits) {
        std::array<std::array<ViPixel, cached_width>, 3> rows{};
        std::size_t written = 0;
        while (written < output.size()) {
            const std::size_t count = std::min(tile_width, output.size() - written);
            const s32 tile_x = x + static_cast<s32>(written);
            const std::size_t source_width = count + halo * 2;
            const unsigned source_rows = repeat_lower ? 2U : 3U;
            for (unsigned row = 0; row < source_rows; ++row) {
                const s32 source_y = y + (static_cast<s32>(row) - 1);
                for (std::size_t column = 0; column < source_width; ++column)
                    rows[row][column] =
                        fetch(tile_x - static_cast<s32>(halo) + static_cast<s32>(column), source_y);
            }

            const auto cached_fetch = [&](s32 source_x, s32 source_y) {
                const s32 row = source_y - y + 1;
                const s32 column = source_x - tile_x + static_cast<s32>(halo);
                assert(row >= 0 && row < 3);
                assert(column >= 0 && static_cast<std::size_t>(column) < source_width);
                return rows[static_cast<unsigned>(row)][static_cast<std::size_t>(column)];
            };
            const auto cached_reconstruct = [&](s32 source_x) {
                return reconstruct_pixel(control_, repeat_lower,
                                         [&](s32 dx, s32 dy) { return cached_fetch(source_x + dx, y + dy); });
            };
            auto tile_output = output.subspan(written, count);
            if ((control_ & 16U) == 0) {
                for (std::size_t index = 0; index < count; ++index)
                    tile_output[index] = cached_reconstruct(tile_x + static_cast<s32>(index)).color;
            } else {
                auto left = cached_reconstruct(tile_x - 1);
                auto center = cached_reconstruct(tile_x);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto right = cached_reconstruct(tile_x + static_cast<s32>(index) + 1);
                    tile_output[index] = divot(left, center, right);
                    left = center;
                    center = right;
                }
            }
            written += count;
        }
        return;
    }
    if ((control_ & 16U) == 0) {
        for (auto& color : output)
            color = reconstruct(x++, y, repeat_lower).color;
        return;
    }

    auto left = reconstruct(x - 1, y, repeat_lower);
    auto center = reconstruct(x, y, repeat_lower);
    for (auto& color : output) {
        const auto right = reconstruct(++x, y, repeat_lower);
        color = divot(left, center, right);
        left = center;
        center = right;
    }
}

} // namespace cupid
