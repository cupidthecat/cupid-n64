#pragma once

#include "cupid/types.hpp"

namespace cupid {

struct RdpDepth {
    u32 value{};
    u16 delta{1};
};

struct RdpDepthResult {
    bool pass{true};
    bool blend_enabled{};
    bool coverage_wrap{};
    unsigned coverage{};
    unsigned pixel_alpha_shift{};
    unsigned memory_alpha_shift{};
};

[[nodiscard]] u16 rdp_compress_depth(u32 depth);
[[nodiscard]] u32 rdp_decompress_depth(u16 compressed);
[[nodiscard]] unsigned rdp_compress_depth_delta(u16 delta);
[[nodiscard]] RdpDepthResult rdp_test_depth(RdpDepth depth, u16 stored, u8 hidden, unsigned coverage,
                                            unsigned memory_coverage, u64 modes);

} // namespace cupid
