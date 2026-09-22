#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

struct RdpTextureAttributes {
    std::array<u32, 3> value{};
    std::array<u32, 3> dx{};
    std::array<u32, 3> de{};
    std::array<u32, 3> dy{};
};

[[nodiscard]] s16 rdp_perspective_coordinate(s16 coordinate, s16 w);
[[nodiscard]] s32 rdp_perspective_coordinate_wide(s16 coordinate, s16 w, bool& overflow);
[[nodiscard]] std::array<s32, 2> rdp_perspective_point(s16 s, s16 t, s16 w, bool& overflow);

} // namespace cupid
