#include "cupid/bus.hpp"
#include "cupid/rdp/triangle.hpp"
#include "raster_tasks.hpp"

#include <algorithm>
#include <bit>

namespace cupid {

void Rdp::color_triangle() {
    const u64 command = buffered_word(0);
    const unsigned opcode = static_cast<unsigned>(command >> 56U) & 63U;
    const auto geometry =
        rdp_triangle_geometry({command, buffered_word(8), buffered_word(16), buffered_word(24)});
    const std::array<unsigned, 4> scissor{scissor_x0_, scissor_y0_, scissor_x1_, scissor_y1_};
    const s32 first_y = std::max(geometry.top, static_cast<s32>(scissor_y0_));
    const s32 last_y = std::min(geometry.bottom, static_cast<s32>(scissor_y1_));
    if (first_y >= last_y)
        return;

    std::array<RdpVarying, 8> attributes{};
    unsigned offset = 32;
    const auto load_group = [&](unsigned first, unsigned count) {
        const auto component = [&](unsigned start, unsigned index) {
            const unsigned shift = 48U - 16U * index;
            return static_cast<u32>(((buffered_word(offset + start) >> shift) & 65535U) << 16U) |
                   static_cast<u32>((buffered_word(offset + start + 16U) >> shift) & 65535U);
        };
        for (unsigned i = 0; i < count; ++i)
            attributes[first + i] = {component(0, i), component(8, i), component(32, i), component(40, i)};
        offset += 64;
    };
    if ((opcode & 4U) != 0)
        load_group(0, 4);
    if ((opcode & 2U) != 0)
        load_group(4, 3);
    if ((opcode & 1U) != 0)
        attributes[7] = {static_cast<u32>(buffered_word(offset) >> 32U),
                         static_cast<u32>(buffered_word(offset)),
                         static_cast<u32>(buffered_word(offset + 8U) >> 32U),
                         static_cast<u32>(buffered_word(offset + 8U))};
    const bool primitive_depth = (other_modes_ & 4U) != 0;
    if (primitive_depth)
        attributes[7] = {static_cast<u32>(primitive_depth_ & 0x7fffU) << 16U, 0, 0, 0};
    const u16 delta = primitive_depth ? primitive_delta_depth_ : rdp_triangle_depth_delta(attributes[7]);
    const bool two_cycles = ((other_modes_ >> 52U) & 3U) == 1U;
    const bool perspective = (other_modes_ & (1ULL << 51U)) != 0;
    const bool depth_value_needed = (other_modes_ & 0x30U) != 0;
    const bool compare_depth = (other_modes_ & (1ULL << 4U)) != 0;
    const bool image_read = (other_modes_ & (1ULL << 6U)) != 0;
    const bool antialias = (other_modes_ & (1ULL << 3U)) != 0;
    const bool early_depth_test = compare_depth && !image_read && (other_modes_ & (1ULL << 12U)) == 0;
    unsigned texture_inputs = rdp_combiner_texture_inputs(color_state_.combine, two_cycles);
    if ((other_modes_ & (1ULL << 48U)) != 0)
        texture_inputs |= 4U;
    const bool lod_needed = (texture_inputs & 4U) != 0;
    const bool one_cycle_texel1_needed = !two_cycles && (texture_inputs & 2U) != 0;
    const unsigned tile = static_cast<unsigned>(command >> 48U) & 7U;
    const unsigned maximum_level = static_cast<unsigned>(command >> 51U) & 7U;
    const s32 direction = geometry.left_major ? 1 : -1;

    const auto render = [&](unsigned first, unsigned last) {
        for (unsigned y = first; y < last; ++y) {
            if (scissor_field_enabled_ && (y & 1U) != static_cast<unsigned>(scissor_keep_odd_))
                continue;
            const auto span = rdp_triangle_span(geometry, scissor, y);
            if (!span.valid)
                continue;
            const auto origin = rdp_triangle_origin(geometry, y);
            std::array<u32, 8> base{};
            for (unsigned i = 0; i < base.size(); ++i)
                base[i] = rdp_varying_base(attributes[i], origin);
            const auto divide = [&](const std::array<s16, 3>& stw, bool& overflow) {
                return perspective ? rdp_perspective_point(stw[0], stw[1], stw[2], overflow)
                                   : RdpTexturePoint{stw[0], stw[1]};
            };
            const auto texture_point = [&](s32 dx, bool next_y, bool& overflow) {
                std::array<s16, 3> stw{};
                for (unsigned i = 0; i < 3; ++i) {
                    const auto varying = attributes[4U + i];
                    const u32 value = base[4U + i] + (varying.dx & ~31U) * static_cast<u32>(dx) +
                                      (next_y ? varying.dy & ~32767U : 0U);
                    stw[i] = std::bit_cast<s16>(static_cast<u16>(value >> 16U));
                }
                return divide(stw, overflow);
            };
            const s32 lod_length = geometry.left_major ? static_cast<s32>(span.end) - origin.x
                                                       : origin.x - static_cast<s32>(span.start);
            const bool lookahead_row = one_cycle_texel1_needed && lod_length >= 8 &&
                                       !scissor_field_enabled_ &&
                                       rdp_triangle_span(geometry, scissor, y + 1U).valid;
            RdpTexturePoint next_row_point{};
            if (lookahead_row) {
                const auto next_origin = rdp_triangle_origin(geometry, y + 1U);
                std::array<s16, 3> stw{};
                for (unsigned i = 0; i < 3; ++i)
                    stw[i] = std::bit_cast<s16>(
                        static_cast<u16>(rdp_varying_base(attributes[4U + i], next_origin) >> 16U));
                bool ignored = false;
                next_row_point = divide(stw, ignored);
            }
            const s32 max_left = std::max({span.left[0], span.left[1], span.left[2], span.left[3]});
            const s32 min_right = std::min({span.right[0], span.right[1], span.right[2], span.right[3]});
            s32 cached_next_dx = -0x7fffffff;
            RdpTexturePoint cached_next_x{};
            bool cached_overflow = false;
            for (unsigned step = 0; step <= span.end - span.start; ++step) {
                const unsigned x = geometry.left_major ? span.start + step : span.end - step;
                const unsigned coverage =
                    (static_cast<s32>(x * 8U) >= max_left && static_cast<s32>(x * 8U + 6U) < min_right)
                        ? 0xffU
                        : rdp_triangle_coverage(span, x);
                if (coverage == 0 || ((other_modes_ & 8U) == 0 && (coverage & 1U) == 0))
                    continue;
                const s32 dx = static_cast<s32>(x) - origin.x;
                const RdpDepth depth{
                    depth_value_needed ? rdp_interpolate_depth(base[7], attributes[7], dx, coverage) : 0U,
                    delta};
                RdpDepthResult tested{};
                if (early_depth_test) {
                    const u32 pixel = y * color_image_width_ + x;
                    const u32 depth_address = framebuffer_address(depth_image_address_, 2, pixel);
                    const auto stored_depth = bus_.memory.read_halfword(depth_address);
                    tested = rdp_test_depth(depth, stored_depth.value, stored_depth.hidden,
                                            static_cast<unsigned>(std::popcount(coverage)), 7U, other_modes_);
                    if (!tested.pass || (antialias && tested.coverage == 0U))
                        continue;
                }
                RdpColorInputs inputs;
                if (texture_inputs != 0) {
                    bool overflow = false;
                    RdpTexturePoint point{};
                    if ((lod_needed || one_cycle_texel1_needed) && dx == cached_next_dx) {
                        point = cached_next_x;
                        overflow = cached_overflow;
                    } else {
                        point = texture_point(dx, false, overflow);
                    }
                    RdpTexturePoint next_x{};
                    RdpTexturePoint next_y{};
                    RdpTexturePoint next_pixel{};
                    if (lod_needed || one_cycle_texel1_needed) {
                        next_x = texture_point(dx + direction, false, overflow);
                        cached_next_x = next_x;
                        cached_overflow = overflow;
                        cached_next_dx = dx + direction;
                    }
                    if (lod_needed)
                        next_y = texture_point(dx, true, overflow);
                    if (one_cycle_texel1_needed)
                        next_pixel = lookahead_row && step == span.end - span.start ? next_row_point : next_x;
                    inputs = sample_color_textures({point, next_x, next_y, next_pixel, overflow}, tile,
                                                   texture_inputs, maximum_level);
                }
                for (unsigned i = 0; i < inputs.shade.size(); ++i)
                    inputs.shade[i] = rdp_interpolate_shade(base[i], attributes[i], dx, coverage);
                write_color_pixel(x, y, coverage, inputs, depth, early_depth_test ? &tested : nullptr);
            }
        }
    };
    const unsigned first = static_cast<unsigned>(first_y / 4);
    const unsigned last = static_cast<unsigned>((last_y + 3) / 4);
    bool parallel = false;
    if (parallel_rasterization_ && last - first >= 8U) {
        // A wide scissor can contain a narrow triangle. Estimate useful work from
        // its middle span before paying to wake the raster workers; the complete
        // scissor bounds still decide whether their memory accesses are independent.
        const auto middle = rdp_triangle_span(geometry, scissor, first + (last - first) / 2U);
        if (middle.valid && middle.end >= middle.start &&
            static_cast<u64>(last - first) * (middle.end - middle.start + 1U) >= 4096U)
            parallel = parallel_rows(first, last, scissor_x0_ / 4U, (scissor_x1_ + 3U) / 4U);
    }
    rdp::raster_rows(bus_.memory, first, last, parallel, render);
}

} // namespace cupid
