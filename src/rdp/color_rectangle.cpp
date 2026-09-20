#include "cupid/bus.hpp"

#include <algorithm>
#include <bit>

namespace cupid {

void Rdp::color_rectangle(u64 command, bool flipped) {
    if (color_image_format_ != 0U || color_image_size_ < 2U)
        return;
    const unsigned left = std::max<unsigned>((command >> 12U) & 4095U, scissor_x0_);
    const unsigned right = std::min<unsigned>((command >> 44U) & 4095U, scissor_x1_);
    const unsigned top = std::max<unsigned>(command & 4095U, scissor_y0_);
    const unsigned bottom = std::min<unsigned>((command >> 32U) & 4095U, scissor_y1_);
    if (left >= right || top >= bottom)
        return;
    const bool untextured = ((command >> 56U) & 63U) == 0x36U;
    const u64 attributes = untextured ? 0U : buffered_word(8);
    const unsigned tile = untextured ? 0U : static_cast<unsigned>((command >> 24U) & 7U);
    const unsigned raw_left = static_cast<unsigned>((command >> 12U) & 4095U);
    const unsigned raw_top = static_cast<unsigned>(command & 4095U);
    const bool perspective = (other_modes_ & (1ULL << 51U)) != 0;
    const RdpDepth depth =
        (other_modes_ & 4U) != 0
            ? RdpDepth{static_cast<u32>(primitive_depth_ & 0x7fffU) << 3U, primitive_delta_depth_}
            : RdpDepth{};
    unsigned texture_inputs =
        rdp_combiner_texture_inputs(color_state_.combine, ((other_modes_ >> 52U) & 3U) == 1U);
    if ((other_modes_ & (1ULL << 48U)) != 0)
        texture_inputs |= 4U;
    const u32 ds = static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(static_cast<u16>(attributes >> 16U))))
                   << 11U;
    const u32 dt = static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(static_cast<u16>(attributes))))
                   << 11U;
    const std::array<u32, 2> dx = {flipped ? 0U : ds, flipped ? dt : 0U};
    const std::array<u32, 2> dy = {flipped ? ds : 0U, flipped ? 0U : dt};
    std::array<u32, 2> origin = {static_cast<u32>(attributes >> 48U) << 16U,
                                 static_cast<u32>((attributes >> 32U) & 65535U) << 16U};
    for (unsigned axis = 0; axis < 2; ++axis) {
        const u32 step = static_cast<u32>((std::bit_cast<s32>(dx[axis]) >> 8) & ~1);
        origin[axis] = ((origin[axis] - (raw_left & 3U) * 64U * step) & ~1023U) - dx[axis] * (raw_left / 4U) -
                       dy[axis] * (raw_top / 4U);
    }
    const auto point_at = [&](unsigned x, unsigned y, bool lod_y = false) {
        RdpTexturePoint result{};
        for (unsigned axis = 0; axis < 2; ++axis) {
            const u32 raw = origin[axis] + dx[axis] * x + dy[axis] * y + (lod_y ? dy[axis] & ~32767U : 0U);
            result[axis] = perspective ? 32767 : std::bit_cast<s32>(raw) >> 16;
        }
        return result;
    };
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
            RdpColorInputs inputs;
            if (texture_inputs != 0) {
                const bool next_row = x == right / 4U && right / 4U - raw_left / 4U >= 8U &&
                                      (y + 1U) * 4U < bottom && !scissor_field_enabled_;
                const RdpTextureCoordinates coordinates{
                    point_at(x, y), point_at(x + 1U, y), point_at(x, y, true),
                    next_row ? point_at(raw_left / 4U, y + 1U) : point_at(x + 1U, y), perspective};
                inputs = sample_color_textures(coordinates, tile, texture_inputs);
            }
            write_color_pixel(x, y, coverage, inputs, depth);
        }
    }
}

} // namespace cupid
