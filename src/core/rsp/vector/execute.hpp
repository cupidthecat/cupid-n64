#pragma once

#include "core/rsp/state.hpp"

namespace cupid::n64 {

void execute_vector_scalar(RspState &state, std::uint32_t instruction);
bool execute_vector_simd(RspState &state, std::uint32_t instruction);
bool vector_simd_available();

} // namespace cupid::n64
