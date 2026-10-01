#pragma once

#include "cupid/types.hpp"

#include <array>
#include <bit>

namespace cupid::rdp {
inline constexpr std::array<u8, 256> sample_counts = [] {
    std::array<u8, 256> values{};
    for (unsigned mask = 0; mask < values.size(); ++mask)
        values[mask] = static_cast<u8>(std::popcount(mask));
    return values;
}();

inline unsigned sample_count(u8 mask) {
    return sample_counts[mask];
}
} // namespace cupid::rdp
