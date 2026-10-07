#include "core/rsp/vector/scalar/kernel.hpp"
#include <utility>

namespace cupid::n64 {
namespace {

template <unsigned Operation, unsigned Element>
void handler(RspState *state, RspVector *dest, const RspVector *source, const RspVector *target) {
  scalar::execute<Operation, Element>(*state, *dest, *source, *target,
                                      static_cast<unsigned>(source - state->vectors.data()));
}

template <std::size_t... Index> constexpr auto handlers(std::index_sequence<Index...>) {
  return std::array<VectorHandler, sizeof...(Index)>{&handler<Index / 16, Index % 16>...};
}

} // namespace

VectorHandler vector_scalar_handler(unsigned operation, unsigned element) {
  static constexpr auto table = handlers(std::make_index_sequence<1024>{});
  return table[operation * 16 + element];
}

void execute_vector_scalar(RspState &state, std::uint32_t instruction) {
  const auto source = (instruction >> 11) & 31;
  scalar::execute(state, state.vectors[(instruction >> 6) & 31], state.vectors[source],
                  state.vectors[(instruction >> 16) & 31], source, instruction & 63,
                  (instruction >> 21) & 15);
}

} // namespace cupid::n64
