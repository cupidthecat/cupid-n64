#pragma once

#include "host_environment.hpp"

#include <type_traits>

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace cupid::fpu_host {

template <typename T> struct BinaryResult {
    T value;
    int exceptions;
};

template <typename T> BinaryResult<T> binary_portable(unsigned function, T left, T right, int rounding) {
    const ScopedEnvironment environment(rounding);
    std::feclearexcept(FE_ALL_EXCEPT);
    volatile T result{};
    switch (function) {
    case 0:
        result = left + right;
        break;
    case 1:
        result = left - right;
        break;
    case 2:
        result = left * right;
        break;
    default:
        result = left / right;
        break;
    }
    const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
    return {result, exceptions};
}

template <typename T> BinaryResult<T> binary(unsigned function, T left, T right, int rounding) {
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned saved = _mm_getcsr();
    unsigned mode = 0;
    switch (rounding) {
    case FE_DOWNWARD:
        mode = 0x2000U;
        break;
    case FE_UPWARD:
        mode = 0x4000U;
        break;
    case FE_TOWARDZERO:
        mode = 0x6000U;
        break;
    default:
        break;
    }
    // Scalar SSE arithmetic owns only MXCSR; leave the caller's x87 state untouched.
    _mm_setcsr(0x1f80U | mode);
    T result{};
    if constexpr (std::is_same_v<T, float>) {
        const auto a = _mm_set_ss(left);
        const auto b = _mm_set_ss(right);
        __m128 value;
        switch (function) {
        case 0:
            value = _mm_add_ss(a, b);
            break;
        case 1:
            value = _mm_sub_ss(a, b);
            break;
        case 2:
            value = _mm_mul_ss(a, b);
            break;
        default:
            value = _mm_div_ss(a, b);
            break;
        }
        result = _mm_cvtss_f32(value);
    } else {
        static_assert(std::is_same_v<T, double>);
        const auto a = _mm_set_sd(left);
        const auto b = _mm_set_sd(right);
        __m128d value;
        switch (function) {
        case 0:
            value = _mm_add_sd(a, b);
            break;
        case 1:
            value = _mm_sub_sd(a, b);
            break;
        case 2:
            value = _mm_mul_sd(a, b);
            break;
        default:
            value = _mm_div_sd(a, b);
            break;
        }
        result = _mm_cvtsd_f64(value);
    }
    const unsigned flags = _mm_getcsr();
    _mm_setcsr(saved);
    const int exceptions = ((flags & 1U) != 0 ? FE_INVALID : 0) | ((flags & 4U) != 0 ? FE_DIVBYZERO : 0) |
                           ((flags & 8U) != 0 ? FE_OVERFLOW : 0) | ((flags & 16U) != 0 ? FE_UNDERFLOW : 0) |
                           ((flags & 32U) != 0 ? FE_INEXACT : 0);
    return {result, exceptions};
#else
    return binary_portable(function, left, right, rounding);
#endif
}

} // namespace cupid::fpu_host
