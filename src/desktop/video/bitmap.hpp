#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace cupid::desktop {

std::vector<std::uint8_t> bitmap_image(unsigned width, unsigned height,
                                       std::span<const std::uint8_t> bgra);

} // namespace cupid::desktop
