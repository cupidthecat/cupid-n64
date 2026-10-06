#pragma once

#include <cstdint>

namespace cupid::n64 {

enum class VideoRegion { Ntsc, Pal };
inline constexpr std::uint32_t clock_frequency = 187500000;

constexpr std::uint32_t video_frequency(VideoRegion region) {
  return region == VideoRegion::Pal ? 49656530 : 48681818;
}

} // namespace cupid::n64
