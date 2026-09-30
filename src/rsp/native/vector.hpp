#pragma once

#include "cupid/types.hpp"

#if defined(CUPID_RSP_NATIVE)
#include <sljitLir.h>

namespace cupid::rsp_native {

struct AccumulatorCache {
    bool enabled{};
    u8 valid{}, dirty{};
};
void flush_accumulator(sljit_compiler* compiler, AccumulatorCache& cache);

[[nodiscard]] bool supports_vector(unsigned function);
void emit_vector(sljit_compiler* compiler, u32 word, bool& comparison_bias_live, bool destination_live,
                 AccumulatorCache& cache);

} // namespace cupid::rsp_native
#endif
