#include "core/rsp/vector/execute.hpp"
#include "core/rsp/rsp.hpp"

namespace cupid::n64 {

void Rsp::vector_execute(std::uint32_t instruction) {
  const auto operation = instruction & 63;
  if (operation >= 0x30 && operation <= 0x36) {
    vector_divide(operation, (instruction >> 6) & 31, (instruction >> 11) & 7,
                  (instruction >> 16) & 31, (instruction >> 21) & 15);
    return;
  }
  if (!execute_vector_simd(state_, instruction))
    execute_vector_scalar(state_, instruction);
}

VectorHandler vector_handler(unsigned operation, unsigned element) {
  if (operation >= 0x30 && operation <= 0x36)
    return vector_divide_handler(operation, element);
  if (vector_simd_available())
    if (auto handler = vector_sse_handler(operation, element))
      return handler;
  return vector_scalar_handler(operation, element);
}

} // namespace cupid::n64
