#pragma once

#include <cmath>
#include <cstdint>

namespace cupid::n64::floating {

#if (defined(_M_X64) || defined(__x86_64__)) &&                                                    \
    (defined(__SSE4_1__) || (defined(_MSC_VER) && !defined(__clang__)))
inline constexpr bool vector_integer_rounding = true;
#else
inline constexpr bool vector_integer_rounding = false;
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
inline constexpr bool nearest_integer_even = true;
#else
inline constexpr bool nearest_integer_even = vector_integer_rounding;
#endif

template <typename Integer, typename Float> Integer round_integer(Float value, unsigned rounding) {
  if (rounding == 0 && !nearest_integer_even)
    return static_cast<Integer>(std::round(value));
  if (!vector_integer_rounding) {
    if (rounding == 2)
      return static_cast<Integer>(std::ceil(value));
    if (rounding == 3)
      return static_cast<Integer>(std::floor(value));
  }
  return static_cast<Integer>(std::nearbyint(value));
}

inline bool integer_rounding_inexact(unsigned rounding) {
  return vector_integer_rounding || (rounding != 2 && rounding != 3);
}

} // namespace cupid::n64::floating
