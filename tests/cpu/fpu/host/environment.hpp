#pragma once
#include "../../../support/test.hpp"
#include <cfenv>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
#include "core/cpu/fpu/host/arm64.hpp"
#endif

namespace test::cpu_fpu {
class HostEnvironment {
public:
  explicit HostEnvironment(unsigned trial) {
#if defined(_M_X64) || defined(__x86_64__)
    saved_ = _mm_getcsr();
    expected_ =
        0x1f80 | ((trial & 3) << 13) | ((trial & 4) ? 0x8040 : 0) | ((trial & 8) ? 0x3f : 0);
    _mm_setcsr(expected_);
#elif defined(_M_ARM64) || defined(__aarch64__)
    saved_ = cupid::n64::floating::arm64::read();
    expected_.control = ((trial & 3) << 22) | ((trial & 4) ? 0x03000000 : 0);
    expected_.status = (trial & 8) ? 0x0800009f : 0;
    cupid::n64::floating::arm64::write(expected_);
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
#elif defined(_M_ARM64) || defined(__aarch64__)
    const auto restored = cupid::n64::floating::arm64::read();
    equal(restored.control, expected_.control);
    equal(restored.status, expected_.status);
    cupid::n64::floating::arm64::write(saved_);
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
#elif defined(_M_ARM64) || defined(__aarch64__)
  cupid::n64::floating::arm64::State saved_{}, expected_{};
#else
  std::fenv_t saved_{};
  int expected_ = 0, flags_ = 0;
#endif
};
} // namespace test::cpu_fpu
