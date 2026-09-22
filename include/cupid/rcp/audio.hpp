#pragma once

#include "cupid/types.hpp"

namespace cupid {

struct AudioSample {
    s16 left{};
    s16 right{};
    u64 rcp_cycle{};
    u32 rate_numerator{44100};
    u32 rate_denominator{1};
    bool from_dma{};
};

} // namespace cupid
