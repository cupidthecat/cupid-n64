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

void Rdp::write_color_pixel(unsigned x, unsigned y, unsigned coverage_mask, const RdpColorInputs& inputs,
                            RdpDepth depth) {
    const unsigned rgb_mode = static_cast<unsigned>(other_modes_ >> 38U) & 3U;
    const unsigned alpha_mode = static_cast<unsigned>(other_modes_ >> 36U) & 3U;
    const unsigned dither_y = scissor_field_enabled_ ? y >> 1U : y;
    const unsigned threshold = dither_matrix[rgb_mode & 1U][(dither_y & 3U) * 4U + (x & 3U)];
    unsigned alpha_dither = 0;
    if (alpha_mode < 2U)
        alpha_dither = alpha_mode == 0U ? threshold : threshold ^ 7U;
    const auto combined = rdp_combine(color_state_, other_modes_, inputs,
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
    const u32 depth_address = depth_image_address_ + (y * color_image_width_ + x) * 2U;
    const bool compare_depth = (other_modes_ & (1ULL << 4U)) != 0;
    const u16 stored_depth = compare_depth ? static_cast<u16>(bus_.memory.read(depth_address, 2)) : 0;
    const u8 hidden_depth = compare_depth ? bus_.memory.hidden_pair(depth_address) : 0;
    const auto tested =
        rdp_test_depth(depth, stored_depth, hidden_depth, combined.coverage, old_coverage, other_modes_);
    if (!tested.pass || (antialias && tested.coverage == 0U))
        return;
    const unsigned shade_alpha = std::min(255U, static_cast<unsigned>(inputs.shade[3]) + alpha_dither);
    RdpColor color =
        rdp_blend(color_state_, other_modes_, combined.color, memory, shade_alpha, tested.blend_enabled,
                  tested.coverage_wrap, tested.memory_alpha_shift, tested.pixel_alpha_shift);
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
            tested.blend_enabled ? std::min(7U, old_coverage + tested.coverage) : (tested.coverage - 1U) & 7U;
        break;
    case 1:
        coverage = (old_coverage + tested.coverage) & 7U;
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
    if ((other_modes_ & (1ULL << 5U)) != 0) {
        const unsigned delta = rdp_compress_depth_delta(depth.delta);
        bus_.memory.write(depth_address, 2,
                          (static_cast<u32>(rdp_compress_depth(depth.value)) << 2U) | (delta >> 2U));
        bus_.memory.set_hidden_pair(depth_address, static_cast<u8>(delta & 3U));
    }
}

} // namespace cupid
