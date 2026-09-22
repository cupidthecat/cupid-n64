#pragma once

#include "cupid/types.hpp"

#include <vector>

namespace cupid {

enum class VideoScanMode { Sequential, Parallel };

struct VideoField {
    unsigned width{640};
    unsigned height{};
    unsigned field{};
    bool interlaced{};
    // Packed RGBA8888, one row per scanline in the current field.
    std::vector<u32> pixels;
};

} // namespace cupid
