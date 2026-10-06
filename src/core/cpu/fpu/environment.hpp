#pragma once

#include <cfenv>
#include <cstdint>

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace cupid::n64::floating {

inline constexpr unsigned Inexact = 1;
inline constexpr unsigned Underflow = 2;
inline constexpr unsigned Overflow = 4;
inline constexpr unsigned DivisionByZero = 8;
inline constexpr unsigned Invalid = 16;

class Environment {
public:
  explicit Environment(unsigned rounding) {
#if defined(_M_X64) || defined(__x86_64__)
    saved_ = _mm_getcsr();
    constexpr unsigned modes[] = {0, 0x6000, 0x4000, 0x2000};
    _mm_setcsr(0x1f80 | modes[rounding & 3]);
#else
    std::feholdexcept(&saved_);
    constexpr int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    std::fesetround(modes[rounding & 3]);
#endif
  }

  ~Environment() {
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(saved_);
#else
    std::fesetenv(&saved_);
#endif
  }

  Environment(const Environment &) = delete;
  Environment &operator=(const Environment &) = delete;

  unsigned exceptions() const {
#if defined(_M_X64) || defined(__x86_64__)
    const auto status = _mm_getcsr();
    return ((status & 0x20) ? Inexact : 0) | ((status & 0x10) ? Underflow : 0) |
           ((status & 0x08) ? Overflow : 0) | ((status & 0x04) ? DivisionByZero : 0) |
           ((status & 0x01) ? Invalid : 0);
#else
    const auto status = std::fetestexcept(FE_ALL_EXCEPT);
    return ((status & FE_INEXACT) ? Inexact : 0) | ((status & FE_UNDERFLOW) ? Underflow : 0) |
           ((status & FE_OVERFLOW) ? Overflow : 0) |
           ((status & FE_DIVBYZERO) ? DivisionByZero : 0) | ((status & FE_INVALID) ? Invalid : 0);
#endif
  }

private:
#if defined(_M_X64) || defined(__x86_64__)
  std::uint32_t saved_;
#else
  std::fenv_t saved_;
#endif
};

} // namespace cupid::n64::floating
