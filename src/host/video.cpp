#include "cupid/host/video.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace cupid::host {
namespace {

bool explicit_blank(const VideoField& field) {
    return field.height == 0 && field.field == 0 && !field.interlaced && field.pixels.empty() &&
           (field.width == 0 || field.width == max_video_field_width);
}

bool valid_field(const VideoField& field) {
    if (field.width == 0 || field.height == 0 || field.width > max_video_field_width ||
        field.height > max_video_field_height || field.field > 1)
        return false;
    if (static_cast<std::size_t>(field.width) >
        std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(field.height))
        return false;
    return field.pixels.size() == static_cast<std::size_t>(field.width) * field.height;
}

bool matching_mode(const VideoField& first, const VideoField& second) {
    return first.width == second.width && first.height == second.height &&
           first.interlaced == second.interlaced;
}

DisplayFrame bob(const VideoField& field) {
    DisplayFrame output;
    output.width = field.width;
    output.height = field.height * 2U;
    output.pixels.resize(static_cast<std::size_t>(output.width) * output.height);
    for (unsigned y = 0; y < field.height; ++y) {
        const auto input =
            field.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * field.width);
        const auto first = output.pixels.begin() +
                           static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y * 2U) * output.width);
        std::copy_n(input, field.width, first);
        std::copy_n(input, field.width, first + static_cast<std::ptrdiff_t>(output.width));
    }
    return output;
}

DisplayFrame weave(const VideoField& first, const VideoField& second) {
    DisplayFrame output;
    output.width = second.width;
    output.height = second.height * 2U;
    output.pixels.resize(static_cast<std::size_t>(output.width) * output.height);
    const VideoField* fields[2]{};
    fields[first.field] = &first;
    fields[second.field] = &second;
    for (unsigned y = 0; y < second.height; ++y) {
        for (unsigned parity = 0; parity < 2; ++parity) {
            const auto input = fields[parity]->pixels.begin() +
                               static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * output.width);
            const auto output_row =
                output.pixels.begin() +
                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y * 2U + parity) * output.width);
            std::copy_n(input, output.width, output_row);
        }
    }
    return output;
}

} // namespace

std::optional<DisplayFrame> FrameComposer::compose(VideoField field) {
    if (explicit_blank(field)) {
        reset();
        return DisplayFrame{};
    }
    if (!valid_field(field))
        return std::nullopt;

    if (previous_field_ && !matching_mode(*previous_field_, field))
        previous_field_.reset();

    if (!field.interlaced) {
        previous_field_.reset();
        return DisplayFrame{field.width, field.height, std::move(field.pixels)};
    }

    DisplayFrame output;
    if (previous_field_ && previous_field_->field != field.field)
        output = weave(*previous_field_, field);
    else
        output = bob(field);
    previous_field_ = std::move(field);
    return output;
}

void FrameComposer::reset() {
    previous_field_.reset();
}

DisplayRect letterbox_4_3(unsigned window_width, unsigned window_height) {
    if (window_width == 0 || window_height == 0)
        return {};

    unsigned width = window_width;
    unsigned height = window_height;
    if (static_cast<u64>(window_width) * 3U <= static_cast<u64>(window_height) * 4U) {
        height = static_cast<unsigned>((static_cast<u64>(window_width) * 3U + 2U) / 4U);
    } else {
        width = static_cast<unsigned>((static_cast<u64>(window_height) * 4U + 1U) / 3U);
    }
    width = std::min(width, window_width);
    height = std::min(height, window_height);
    return {(window_width - width) / 2U, (window_height - height) / 2U, width, height};
}

} // namespace cupid::host
