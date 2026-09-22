#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

struct RdpTriangleEdge {
    u32 position{};
    u32 step{};
};

struct RdpTriangleGeometry {
    s32 top{}, middle{}, bottom{};
    bool left_major{};
    bool offset_latch{};
    RdpTriangleEdge lower, major, upper;
};

struct RdpTriangleSpan {
    std::array<s32, 4> left{65535, 65535, 65535, 65535};
    std::array<s32, 4> right{};
    unsigned start{}, end{};
    bool valid{};
};

struct RdpTriangleOrigin {
    s32 x{}, rows{};
    unsigned fraction{};
    bool offset_latch{};
};

struct RdpVarying {
    u32 value{}, dx{}, de{}, dy{};
};

[[nodiscard]] RdpTriangleGeometry rdp_triangle_geometry(const std::array<u64, 4>& words);
[[nodiscard]] RdpTriangleSpan rdp_triangle_span(const RdpTriangleGeometry& geometry,
                                                const std::array<unsigned, 4>& scissor, unsigned y);
[[nodiscard]] unsigned rdp_triangle_coverage(const RdpTriangleSpan& span, unsigned x);
[[nodiscard]] RdpTriangleOrigin rdp_triangle_origin(const RdpTriangleGeometry& geometry, unsigned y);
[[nodiscard]] u32 rdp_varying_base(RdpVarying varying, RdpTriangleOrigin origin);
[[nodiscard]] s32 rdp_interpolate_shade(u32 base, RdpVarying varying, s32 dx, unsigned coverage);
[[nodiscard]] u32 rdp_interpolate_depth(u32 base, RdpVarying varying, s32 dx, unsigned coverage);
[[nodiscard]] u16 rdp_triangle_depth_delta(RdpVarying varying);

} // namespace cupid
