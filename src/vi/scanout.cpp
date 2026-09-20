#include "cupid/system.hpp"
#include "cupid/vi/filter.hpp"

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
    const ViFilter filter(rdram, memory.hidden_memory(), vi_);
    const bool resample = ((vi_[0] >> 8) & 3U) != 3;

    for (s32 y = first_y; y < last_y; ++y) {
        const u32 sample_y = (vi_[13] >> 16) + static_cast<u32>(y - top) * (vi_[13] & 4095U);
        for (s32 x = first_x; x < last_x; ++x) {
            const u32 sample_x = (vi_[12] >> 16) + static_cast<u32>(x - left) * (vi_[12] & 4095U);
            const s32 source_x = static_cast<s32>(sample_x >> 10);
            const s32 source_y = static_cast<s32>(sample_y >> 10);
            const u32 y_step = vi_[13] & 4095U;
            const bool repeat_lower = y_step < 1024 && y != first_y &&
                                      (sample_y >> 10) == ((sample_y - y_step) >> 10) &&
                                      (sample_y >> 10) != ((sample_y + y_step) >> 10);
            const auto read = [&](s32 dx, s32 dy) {
                return filter.sample(source_x + dx, source_y + dy, dy != 0 && repeat_lower);
            };
            Color color = read(0, 0);
            if (resample) {
                const u32 fraction_y = (sample_y >> 5) & 31U;
                const Color column0 = interpolate(color, read(0, 1), fraction_y);
                const Color column1 = interpolate(read(1, 0), read(1, 1), fraction_y);
                color = interpolate(column0, column1, (sample_x >> 5) & 31U);
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
