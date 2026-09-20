#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

using RdpColor = std::array<s32, 4>;

struct RdpColorState {
    u64 combine{};
    u32 primitive{};
    u32 environment{};
    u32 fog{};
    u32 blend{};
    u8 minimum_lod{};
    u8 primitive_lod{};
    std::array<u16, 3> key_width{};
    std::array<u8, 3> key_center{};
    std::array<u8, 3> key_scale{};
    std::array<u16, 6> convert{};
};

struct RdpColorInputs {
    RdpColor texel0{};
    RdpColor texel1{};
    RdpColor shade{};
    s32 lod_fraction{};
    s32 noise{};
};

struct RdpCombinedPixel {
    RdpColor color{};
    unsigned coverage{};
    unsigned test_alpha{};
};

[[nodiscard]] RdpColor rdp_unpack_color(u32 value);
[[nodiscard]] RdpCombinedPixel rdp_combine(const RdpColorState& state, u64 modes, RdpColorInputs inputs,
                                           unsigned coverage, unsigned alpha_dither);
[[nodiscard]] u8 rdp_blend_divide(unsigned numerator, unsigned denominator);
[[nodiscard]] RdpColor rdp_blend(const RdpColorState& state, u64 modes, RdpColor pixel,
                                 const RdpColor& memory, unsigned shade_alpha, bool blend_enabled,
                                 bool coverage_wrap, unsigned memory_alpha_shift);

} // namespace cupid
