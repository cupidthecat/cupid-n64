#include "cupid/bus.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

constexpr unsigned dither_matrix[2][16] = {
    {0, 6, 1, 7, 4, 2, 5, 3, 3, 5, 2, 4, 7, 1, 6, 0},
    {0, 4, 1, 5, 4, 0, 5, 1, 3, 7, 2, 6, 7, 3, 6, 2},
};

} // namespace

void Rdp::color_rectangle(u64 command) {
    if (color_image_format_ != 0U || color_image_size_ < 2U)
        return;
    const unsigned left = std::max<unsigned>((command >> 12U) & 4095U, scissor_x0_);
    const unsigned right = std::min<unsigned>((command >> 44U) & 4095U, scissor_x1_);
    const unsigned top = std::max<unsigned>(command & 4095U, scissor_y0_);
    const unsigned bottom = std::min<unsigned>((command >> 32U) & 4095U, scissor_y1_);
    if (left >= right || top >= bottom)
        return;
    for (unsigned y = top / 4U; y <= (bottom - 1U) / 4U; ++y) {
        if (scissor_field_enabled_ && (y & 1U) != static_cast<unsigned>(scissor_keep_odd_))
            continue;
        for (unsigned x = left / 4U; x <= (right - 1U) / 4U; ++x) {
            unsigned coverage = 0;
            for (unsigned row = 0; row < 4; ++row) {
                if (y * 4U + row < top || y * 4U + row >= bottom)
                    continue;
                for (unsigned sample = 0; sample < 2; ++sample) {
                    const unsigned position = x * 8U + (row & 1U) * 2U + sample * 4U;
                    if (position >= left * 2U && position < right * 2U)
                        coverage |= 1U << (row * 2U + sample);
                }
            }
            write_color_pixel(x, y, coverage);
        }
    }
}

void Rdp::write_color_pixel(unsigned x, unsigned y, unsigned coverage_mask) {
    const unsigned rgb_mode = static_cast<unsigned>(other_modes_ >> 38U) & 3U;
    const unsigned alpha_mode = static_cast<unsigned>(other_modes_ >> 36U) & 3U;
    const unsigned dither_y = scissor_field_enabled_ ? y >> 1U : y;
    const unsigned threshold = dither_matrix[rgb_mode & 1U][(dither_y & 3U) * 4U + (x & 3U)];
    unsigned alpha_dither = 0;
    if (alpha_mode < 2U)
        alpha_dither = alpha_mode == 0U ? threshold : threshold ^ 7U;
    const auto combined = rdp_combine(color_state_, other_modes_, {},
                                      static_cast<unsigned>(std::popcount(coverage_mask)), alpha_dither);
    const bool antialias = (other_modes_ & (1ULL << 3U)) != 0;
    if (antialias ? combined.coverage == 0U : (coverage_mask & 1U) == 0U)
        return;
    if ((other_modes_ & 1U) != 0 && combined.test_alpha < (color_state_.blend & 255U))
        return;

    const unsigned bytes = color_image_size_ == 2U ? 2U : 4U;
    const u32 address = color_image_address_ + (y * color_image_width_ + x) * bytes;
    const u32 stored = static_cast<u32>(bus_.memory.read(address, bytes));
    RdpColor memory;
    if (bytes == 2U) {
        const unsigned coverage = ((stored & 1U) << 2U) | bus_.memory.hidden_pair(address);
        memory = {static_cast<s32>((stored >> 8U) & 248U), static_cast<s32>((stored >> 3U) & 248U),
                  static_cast<s32>((stored << 2U) & 248U), static_cast<s32>(coverage << 5U)};
    } else {
        memory = rdp_unpack_color(stored);
        memory[3] &= 224;
    }
    if ((other_modes_ & (1ULL << 6U)) == 0)
        memory[3] = 224;
    const unsigned old_coverage = static_cast<unsigned>(memory[3]) >> 5U;
    const bool wrap = combined.coverage + old_coverage >= 8U;
    const bool blend_enabled = (other_modes_ & (1ULL << 14U)) != 0 || (antialias && !wrap);
    unsigned dz_log = 0;
    if ((other_modes_ & 4U) != 0) {
        for (unsigned bit = 0; bit < 16; ++bit) {
            if ((primitive_delta_depth_ & (1U << bit)) != 0)
                dz_log |= bit;
        }
    }
    RdpColor color = rdp_blend(color_state_, other_modes_, combined.color, memory, alpha_dither,
                               blend_enabled, wrap, std::min(15U - dz_log, 4U));
    if (rgb_mode < 2U) {
        for (unsigned channel = 0; channel < 3; ++channel) {
            if ((static_cast<unsigned>(color[channel]) & 7U) > threshold)
                color[channel] = std::min((color[channel] & 248) + 8, 255);
        }
    }
    unsigned coverage = old_coverage;
    switch ((other_modes_ >> 8U) & 3U) {
    case 0:
        coverage =
            blend_enabled ? std::min(7U, old_coverage + combined.coverage) : (combined.coverage - 1U) & 7U;
        break;
    case 1:
        coverage = (old_coverage + combined.coverage) & 7U;
        break;
    case 2:
        coverage = 7;
        break;
    default:
        break;
    }
    const unsigned red = static_cast<unsigned>(color[0]);
    const unsigned green = static_cast<unsigned>(color[1]);
    const unsigned blue = static_cast<unsigned>(color[2]);
    if (bytes == 2U) {
        bus_.memory.write(address, 2,
                          ((red & 248U) << 8U) | ((green & 248U) << 3U) | ((blue & 248U) >> 2U) |
                              (coverage >> 2U));
        bus_.memory.set_hidden_pair(address, static_cast<u8>(coverage & 3U));
    } else {
        bus_.memory.write(address, 4, (red << 24U) | (green << 16U) | (blue << 8U) | (coverage << 5U));
    }
}

} // namespace cupid
