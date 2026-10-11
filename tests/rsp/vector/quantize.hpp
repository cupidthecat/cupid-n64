#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace test {

constexpr std::array<std::int32_t, 24> quantization_products{
    std::numeric_limits<std::int32_t>::min(),
    -65538,
    -65536,
    -65535,
    -65,
    -64,
    -63,
    -33,
    -32,
    -31,
    -1,
    0,
    1,
    31,
    32,
    33,
    63,
    64,
    65,
    65535,
    65536,
    65537,
    std::numeric_limits<std::int32_t>::max() - 1,
    std::numeric_limits<std::int32_t>::max()};

} // namespace test
