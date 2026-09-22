#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

[[nodiscard]] u16 rdp_pixel_noise(u32 primitive, unsigned x, unsigned y);
[[nodiscard]] s32 rdp_combiner_noise(u16 sample);
[[nodiscard]] std::array<unsigned, 4> rdp_dither_coefficients(u64 modes, unsigned x, unsigned y, u16 sample);

} // namespace cupid
