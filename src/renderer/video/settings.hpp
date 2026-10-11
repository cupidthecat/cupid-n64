#pragma once

#include <array>
#include <cstddef>

namespace cupid::n64 {

struct RenderSettings {
  unsigned scale = 1;
  bool supersampling = false;
  bool weave = false;
  bool bypass_vi = false;
  static constexpr std::array<unsigned, 4> scales{1, 2, 4, 8};

  constexpr bool valid() const {
    return (scale == 1 || scale == 2 || scale == 4 || scale == 8) &&
           (!supersampling || (scale > 1 && !weave));
  }
  bool operator==(const RenderSettings &) const = default;
};

inline constexpr unsigned maximum_frame_width = 8192;
inline constexpr unsigned maximum_frame_height = 8192;
inline constexpr std::size_t maximum_frame_bytes =
    std::size_t(maximum_frame_width) * maximum_frame_height * 4;

} // namespace cupid::n64
