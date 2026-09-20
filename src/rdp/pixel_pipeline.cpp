#include "cupid/bus.hpp"
#include "cupid/rdp/noise.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
void Rdp::write_color_pixel(unsigned x, unsigned y, unsigned coverage_mask, RdpColorInputs inputs,
                            RdpDepth depth) {
    const unsigned rgb_mode = static_cast<unsigned>(other_modes_ >> 38U) & 3U;
    const unsigned alpha_mode = static_cast<unsigned>(other_modes_ >> 36U) & 3U;
    const bool two_cycles = ((other_modes_ >> 52U) & 3U) == 1U;
    const bool first_noise = two_cycles && ((color_state_.combine >> 52U) & 15U) == 7U;
    const bool last_noise = ((color_state_.combine >> 37U) & 15U) == 7U;
    const bool random_alpha = (other_modes_ & 3U) == 3U;
    const bool needs_noise = rgb_mode == 2U || alpha_mode == 2U || random_alpha || first_noise || last_noise;
    u16 sample = needs_noise ? rdp_pixel_noise(primitive_sequence_, x, y) : 0;
    const unsigned dither_y = scissor_field_enabled_ ? y >> 1U : y;
    const auto dither = rdp_dither_coefficients(other_modes_, x, dither_y, sample);
    const unsigned alpha_dither = dither[3];
    inputs.noise.fill(rdp_combiner_noise(sample));
    if (first_noise && last_noise) {
        sample = rdp_pixel_noise(primitive_sequence_ + 11U, x + 1023U, y + 7U);
        inputs.noise[1] = rdp_combiner_noise(sample);
    }
    const auto combined = rdp_combine(color_state_, other_modes_, inputs,
                                      static_cast<unsigned>(std::popcount(coverage_mask)), alpha_dither);
    const bool antialias = (other_modes_ & (1ULL << 3U)) != 0;
    if (antialias ? combined.coverage == 0U : (coverage_mask & 1U) == 0U)
        return;
    const unsigned alpha_threshold = random_alpha ? sample & 255U : color_state_.blend & 255U;
    if ((other_modes_ & 1U) != 0 && combined.test_alpha < alpha_threshold)
        return;

    const unsigned bytes = color_image_size_ < 2U ? 1U : 1U << (color_image_size_ - 1U);
    const u32 pixel = y * color_image_width_ + x;
    const u32 address = framebuffer_address(color_image_address_, bytes, pixel);
    const RdpColor memory = read_framebuffer_color(address);
    const unsigned old_coverage = static_cast<unsigned>(memory[3]) >> 5U;
    const u32 depth_address = framebuffer_address(depth_image_address_, 2, pixel);
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
    if (rgb_mode < 3U) {
        for (unsigned channel = 0; channel < 3; ++channel) {
            if ((static_cast<unsigned>(color[channel]) & 7U) > dither[channel])
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
    write_framebuffer_color(address, color, coverage);
    if ((other_modes_ & (1ULL << 5U)) != 0) {
        const unsigned delta = rdp_compress_depth_delta(depth.delta);
        const bool depth_alias = color_image_address_ == depth_image_address_;
        if (!depth_alias || color_image_size_ == 2U) {
            bus_.memory.write(depth_address, 2,
                              (static_cast<u32>(rdp_compress_depth(depth.value)) << 2U) | (delta >> 2U));
            const bool intensity_alias = depth_alias && color_image_format_ != 0U;
            const u8 hidden =
                intensity_alias ? static_cast<u8>(((delta >> 2U) & 1U) * 3U) : static_cast<u8>(delta & 3U);
            bus_.memory.set_hidden_pair(depth_address, hidden);
        }
    }
}

} // namespace cupid
