#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

void Rdp::fill_rectangle(u64 command) {
    if (((other_modes_ >> 52U) & 3U) != 3U || color_image_size_ == 0U)
        return;

    const unsigned raw_right = static_cast<unsigned>((command >> 44U) & 0x0fffU);
    const unsigned raw_bottom = static_cast<unsigned>((command >> 32U) & 0x0fffU);
    const unsigned raw_left = static_cast<unsigned>((command >> 12U) & 0x0fffU);
    const unsigned raw_top = static_cast<unsigned>(command & 0x0fffU);
    if (raw_left > raw_right || raw_left >= scissor_x1_ || raw_right < scissor_x0_)
        return;

    // Fill extends the bottom edge to the final quarter-pixel of its row.
    // The subpixel interval remains half-open, even for a one-row rectangle.
    const unsigned y0 = std::max(raw_top, static_cast<unsigned>(scissor_y0_));
    const unsigned y1 = std::min(raw_bottom | 3U, static_cast<unsigned>(scissor_y1_));
    if (y0 >= y1)
        return;

    const auto clip_x = [&](unsigned x) {
        return std::min(std::max(x, static_cast<unsigned>(scissor_x0_)),
                        static_cast<unsigned>(scissor_x1_)) >>
               2U;
    };
    const unsigned left = clip_x(raw_left);
    const unsigned right = clip_x(raw_right);
    const unsigned top = y0 >> 2U;
    const unsigned bottom = (y1 - 1U) >> 2U;
    for (unsigned y = top; y <= bottom; ++y) {
        if (scissor_field_enabled_ && (y & 1U) != static_cast<unsigned>(scissor_keep_odd_))
            continue;
        // Fill uses the inclusive integer span, including a clipped right edge.
        fill_span(y, left, right);
    }
}

void Rdp::fill_span(unsigned y, unsigned left, unsigned right) {
    const unsigned bytes_per_pixel = 1U << (color_image_size_ - 1U);
    for (unsigned x = left; x <= right; ++x) {
        const u32 pixel = y * color_image_width_ + x;
        const u32 address = framebuffer_address(color_image_address_, bytes_per_pixel, pixel);
        if (color_image_size_ == 1U) {
            const u8 color = static_cast<u8>(fill_color_ >> ((3U - (address & 3U)) * 8U));
            const u8 hidden = bus_.memory.hidden_pair(address);
            bus_.memory.write(address, 1, color);
            // Only the odd byte drives the hidden pair in an 8-bit fill.
            bus_.memory.set_hidden_pair(address,
                                        (address & 1U) != 0 ? static_cast<u8>((color & 1U) * 3U) : hidden);
        } else if (color_image_size_ == 2U) {
            const u16 color = static_cast<u16>(fill_color_ >> ((address & 2U) == 0 ? 16U : 0U));
            bus_.memory.write(address, 2, color);
        } else {
            bus_.memory.write(address, 4, fill_color_);
        }
    }
}

} // namespace cupid
