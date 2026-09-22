#include "cupid/vi/filter.hpp"

#include <algorithm>
#include <bit>

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
    ViPixel center = fetch(x, y);
    if (center.coverage != 7) {
        const std::array<ViPixel, 6> neighbors = {
            fetch(x - 1, y - 1),
            fetch(x + 1, y - 1),
            fetch(x - 2, y),
            fetch(x + 2, y),
            repeat_lower ? fetch(x - 2, y) : fetch(x - 1, y + 1),
            repeat_lower ? fetch(x + 2, y) : fetch(x + 1, y + 1),
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
    } else if ((control_ & 0x10000U) != 0) {
        const ViColor original = center.color;
        std::array<s32, 3> adjustment{};
        const auto accumulate = [&](s32 dx, s32 dy) {
            const auto neighbor = fetch(x + dx, y + dy);
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
