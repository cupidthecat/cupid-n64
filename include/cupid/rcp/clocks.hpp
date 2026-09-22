#pragma once

#include "cupid/types.hpp"

#include <limits>

namespace cupid::rcp {

// The carried fraction is 0, 1, or 2 from the CPU-to-RCP 2:3 conversion.
[[nodiscard]] constexpr u64 cpu_cycles_for_rcp(u64 cycles, u64 fraction) {
    if (cycles == 0)
        return 0;
    const u64 pairs = (cycles - 1) / 2;
    const u64 tail = ((cycles - 1) % 2 * 3 + 4 - fraction) / 2;
    constexpr u64 maximum = std::numeric_limits<u64>::max();
    if (pairs > (maximum - tail) / 3)
        return maximum;
    return pairs * 3 + tail;
}

} // namespace cupid::rcp
