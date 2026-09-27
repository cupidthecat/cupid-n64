#include "vector_operations.hpp"

namespace cupid {

bool Rsp::execute_vector_op_sse2(u32 instruction) {
    return execute_vector_op_known<64U>(instruction);
}

} // namespace cupid
