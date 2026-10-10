#pragma once
#include <cfenv>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace test::cpu_replay::fpu::numeric {
class HostEnvironment {
public:
  explicit HostEnvironment(unsigned trial) {
#if defined(_M_X64) || defined(__x86_64__)
    saved_ = _mm_getcsr();
    expected_ =
        0x1f80 | ((trial & 3) << 13) | ((trial & 4) ? 0x8040 : 0) | ((trial & 8) ? 0x3f : 0);
    _mm_setcsr(expected_);
#else
    std::feholdexcept(&saved_);
    constexpr int modes[]{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    expected_ = modes[trial & 3];
    std::fesetround(expected_);
    if (trial & 8)
      std::feraiseexcept(FE_DIVBYZERO | FE_INEXACT);
    flags_ = std::fetestexcept(FE_ALL_EXCEPT);
#endif
  }
  ~HostEnvironment() {
#if defined(_M_X64) || defined(__x86_64__)
    equal(_mm_getcsr(), expected_);
    _mm_setcsr(saved_);
#else
    equal(std::fegetround(), expected_);
    equal(std::fetestexcept(FE_ALL_EXCEPT), flags_);
    std::fesetenv(&saved_);
#endif
  }
  HostEnvironment(const HostEnvironment &) = delete;
  HostEnvironment &operator=(const HostEnvironment &) = delete;

private:
#if defined(_M_X64) || defined(__x86_64__)
  unsigned saved_ = 0, expected_ = 0;
#else
  std::fenv_t saved_{};
  int expected_ = 0, flags_ = 0;
#endif
};
} // namespace test::cpu_replay::fpu::numeric
