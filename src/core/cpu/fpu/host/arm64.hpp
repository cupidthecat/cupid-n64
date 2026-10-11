#pragma once

#include <cstdint>

#if defined(_M_ARM64)
#include <intrin.h>
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
namespace cupid::n64::floating::arm64 {

inline constexpr unsigned rounding_modes[]{0, 3u << 22, 1u << 22, 2u << 22};

struct State {
  std::uint64_t control = 0;
  std::uint64_t status = 0;
};

inline State read() {
  State state;
#if defined(_M_ARM64)
  state.control = _ReadStatusReg(0x5a20);
  state.status = _ReadStatusReg(0x5a21);
#else
  asm volatile("mrs %0, fpcr\n\tmrs %1, fpsr"
               : "=r"(state.control), "=r"(state.status)
               :
               : "memory");
#endif
  return state;
}

inline void write(State state) {
#if defined(_M_ARM64)
  _WriteStatusReg(0x5a20, state.control);
  _WriteStatusReg(0x5a21, state.status);
#else
  asm volatile("msr fpcr, %0\n\tmsr fpsr, %1" : : "r"(state.control), "r"(state.status) : "memory");
#endif
}

} // namespace cupid::n64::floating::arm64
#endif
