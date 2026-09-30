#pragma once

#include "cupid/types.hpp"

#if defined(CUPID_RSP_NATIVE)
#include <sljitLir.h>

namespace cupid::rsp_native {

[[nodiscard]] bool supports_vector(unsigned function);
void emit_vector(sljit_compiler* compiler, u32 word, bool& comparison_bias_live);

} // namespace cupid::rsp_native
#endif
