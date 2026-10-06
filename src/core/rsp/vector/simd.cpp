#include "core/rsp/vector/execute.hpp"
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif

namespace cupid::n64 {

bool execute_vector_sse(RspState &state, std::uint32_t instruction);

bool vector_simd_available() {
  static const bool available = [] {
#if defined(_M_X64) || defined(_M_IX86)
    int registers[4];
    __cpuid(registers, 1);
    return (registers[2] & (1 << 19)) != 0;
#elif defined(__x86_64__) || defined(__i386__)
    return __builtin_cpu_supports("sse4.1") != 0;
#else
    return false;
#endif
  }();
  return available;
}

bool execute_vector_simd(RspState &state, std::uint32_t instruction) {
  return vector_simd_available() && execute_vector_sse(state, instruction);
}

} // namespace cupid::n64
