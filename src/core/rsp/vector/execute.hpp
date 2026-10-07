#pragma once

#include "core/rsp/state.hpp"

namespace cupid::n64 {

using VectorHandler = void (*)(RspState *, RspVector *, const RspVector *, const RspVector *);

VectorHandler vector_handler(unsigned operation, unsigned element);
VectorHandler vector_scalar_handler(unsigned operation, unsigned element);
VectorHandler vector_sse_handler(unsigned operation, unsigned element);
VectorHandler vector_divide_handler(unsigned operation, unsigned element);

void execute_vector_scalar(RspState &state, std::uint32_t instruction);
bool execute_vector_simd(RspState &state, std::uint32_t instruction);
bool vector_simd_available();

} // namespace cupid::n64
