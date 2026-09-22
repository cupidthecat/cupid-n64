#include "cupid/system.hpp"
#include "cupid/vi/filter.hpp"
#include "filtered_rows.hpp"

#include <algorithm>
#include <array>

namespace cupid {
namespace {
using Color = std::array<u32, 3>;

Color interpolate(const Color& first, const Color& second, u32 fraction) {
    Color result{};
    for (unsigned channel = 0; channel < result.size(); ++channel)
        result[channel] = (first[channel] * (32 - fraction) + second[channel] * fraction + 16) >> 5;
    return result;
}

} // namespace

VideoField Bus::scan_video() const {
    const bool pal = system_.video_standard() == VideoStandard::Pal;
    VideoField output;
    output.height = pal ? 288 : 240;
    output.field = vi_current_ & 1U;
    output.interlaced = (vi_[0] & 64U) != 0;
    output.pixels.resize(output.width * output.height, 0x000000ffU);

    const unsigned format = vi_[0] & 3U;
    if (format < 2 || rdram.empty())
        return output;

    const s32 horizontal_offset = pal ? 128 : 108;
    const s32 vertical_offset = pal ? 44 : 34;
    const s32 left = static_cast<s32>(vi_[9] >> 16) - horizontal_offset;
    const s32 right = static_cast<s32>(vi_[9] & 1023U) - horizontal_offset;
    const s32 top = (static_cast<s32>(vi_[10] >> 16) - vertical_offset) / 2;
    const s32 rows = (static_cast<s32>(vi_[10] & 1023U) - static_cast<s32>(vi_[10] >> 16)) / 2;
    const s32 first_x = std::max<s32>(0, left < 0 ? left : left + 8);
    const s32 last_x = std::min<s32>(640, right > 640 ? right : right - 7);
    const s32 first_y = std::max<s32>(0, top);
    const s32 last_y = std::min<s32>(static_cast<s32>(output.height), top + rows);
    if (first_x >= last_x || first_y >= last_y)
        return output;
    const ViFilter filter(rdram, memory.hidden_memory(), vi_);
    const bool resample = ((vi_[0] >> 8) & 3U) != 3;
    const u32 x_step = vi_[12] & 4095U;
    const u32 y_step = vi_[13] & 4095U;
    const auto sample_x_at = [&](s32 x) { return (vi_[12] >> 16) + static_cast<u32>(x - left) * x_step; };
    const u32 first_source_x = sample_x_at(first_x) >> 10;
    const u32 last_source_x = sample_x_at(last_x - 1) >> 10;
    const bool reuse_rows = x_step <= 1024;
    vi::FilteredRows filtered_rows(filter, static_cast<s32>(first_source_x),
                                   last_source_x - first_source_x + 1U + static_cast<unsigned>(resample));

    for (s32 y = first_y; y < last_y; ++y) {
        const u32 sample_y = (vi_[13] >> 16) + static_cast<u32>(y - top) * y_step;
        const s32 source_y = static_cast<s32>(sample_y >> 10);
        const u32 fraction_y = (sample_y >> 5) & 31U;
        const bool lower_needed = resample && fraction_y != 0;
        const bool repeat_lower = y_step < 1024 && y != first_y &&
                                  (sample_y >> 10) == ((sample_y - y_step) >> 10) &&
                                  (sample_y >> 10) != ((sample_y + y_step) >> 10);
        std::span<const ViColor> upper;
        std::span<const ViColor> lower;
        if (reuse_rows) {
            upper = filtered_rows.get(source_y, false);
            if (lower_needed)
                lower = filtered_rows.get(source_y + 1, repeat_lower);
        }
        for (s32 x = first_x; x < last_x; ++x) {
            const u32 sample_x = sample_x_at(x);
            const s32 source_x = static_cast<s32>(sample_x >> 10);
            const auto read = [&](s32 dx, s32 dy) {
                if (reuse_rows) {
                    const auto index = static_cast<unsigned>(source_x + dx) - first_source_x;
                    return (dy == 0 ? upper : lower)[index];
                }
                return filter.sample(source_x + dx, source_y + dy, dy != 0 && repeat_lower);
            };
            Color color = read(0, 0);
            if (resample) {
                if (lower_needed)
                    color = interpolate(color, read(0, 1), fraction_y);
                const u32 fraction_x = (sample_x >> 5) & 31U;
                if (fraction_x != 0) {
                    Color column1 = read(1, 0);
                    if (lower_needed)
                        column1 = interpolate(column1, read(1, 1), fraction_y);
                    color = interpolate(color, column1, fraction_x);
                }
            }
            color = vi_gamma(color, (vi_[0] & 8U) != 0, (vi_[0] & 4U) != 0,
                             vi_gamma_noise(vi_field_sequence_, static_cast<unsigned>(x - left),
                                            static_cast<unsigned>(y - top)));
            output.pixels[static_cast<std::size_t>(y) * output.width + static_cast<unsigned>(x)] =
                (color[0] << 24) | (color[1] << 16) | (color[2] << 8) | 255U;
        }
    }
    return output;
}

} // namespace cupid
