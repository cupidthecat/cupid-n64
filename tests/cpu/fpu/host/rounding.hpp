#pragma once

#include "core/cpu/fpu/rounding.hpp"

namespace test::cpu_fpu {

#if (defined(_M_ARM64) || defined(__aarch64__)) && defined(__GNUC__) && !defined(__clang__) &&     \
    defined(__OPTIMIZE__)
inline constexpr bool conversion_inexact(bool, bool) {
  return true;
}
#else
inline constexpr bool conversion_inexact(bool single, bool wide) {
#if defined(__GNUC__) && !defined(__clang__) && defined(__OPTIMIZE__) &&                           \
    (defined(_M_X64) || defined(__x86_64__))
  if (!cupid::n64::floating::vector_integer_rounding)
    return single || !wide;
#endif
  (void)single;
  (void)wide;
  return cupid::n64::floating::vector_integer_rounding;
}
#endif

} // namespace test::cpu_fpu
