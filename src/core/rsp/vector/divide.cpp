#include "core/rsp/rsp.hpp"
#include "core/rsp/vector/divide/kernel.hpp"
#include "core/rsp/vector/execute.hpp"
#include <bit>
#include <utility>

namespace cupid::n64 {
namespace {

struct DivideTables {
  std::array<std::uint16_t, 512> reciprocal{}, square_root{};
  DivideTables() {
    reciprocal[0] = 0xffff;
    for (unsigned index = 1; index < 512; ++index)
      reciprocal[index] = static_cast<std::uint16_t>((((1ull << 34) / (index + 512)) + 1) >> 8);
    for (unsigned index = 0; index < 512; ++index) {
      const auto a = (index + 512) >> (index & 1);
      std::uint64_t low = 1 << 17;
      std::uint64_t high = 1 << 18;
      while (low < high) {
        const auto middle = (low + high + 1) / 2;
        if (a * middle * middle < (1ull << 44))
          low = middle;
        else
          high = middle - 1;
      }
      square_root[index] = static_cast<std::uint16_t>(low >> 1);
    }
  }
};

const DivideTables tables;

unsigned select_lane(unsigned element, unsigned lane) {
  if (element < 2)
    return lane;
  if (element < 4)
    return (lane & 6) | (element & 1);
  if (element < 8)
    return (lane & 4) | (element & 3);
  return element & 7;
}

} // namespace

std::uint32_t divide::value(std::int32_t input, bool square_root) {
  const auto mask = static_cast<std::uint32_t>(input >> 31);
  auto data = static_cast<std::uint32_t>(input) ^ mask;
  if (input > -32768)
    data -= mask;
  std::uint32_t value;
  if (!data)
    value = 0x7fffffff;
  else if (input == -32768)
    value = 0xffff0000;
  else {
    const auto shift = std::countl_zero(data);
    const auto index = ((std::uint64_t(data) << shift) & 0x7fc00000) >> 22;
    value =
        square_root ? tables.square_root[(index & 0x1fe) | (shift & 1)] : tables.reciprocal[index];
    value = ((0x10000 | value) << 14) >> (square_root ? ((31 - shift) >> 1) : (31 - shift));
    value ^= mask;
  }
  return value;
}

template <int Operation = -1, int Element = -1>
void execute_divide(RspState &state, RspVector &dest, unsigned lane, const RspVector &source,
                    unsigned dynamic_operation = 0, unsigned dynamic_element = 0) {
  const auto element = Element < 0 ? dynamic_element : unsigned(Element);
  const auto vector = source;
  for (unsigned n = 0; n < 8; ++n)
    state.accumulator.low.lanes[n] = vector.lanes[select_lane(element, n)];
  divide::execute_lane<Operation>(state, dest.lanes[lane], vector.lanes[select_lane(element, lane)],
                                  vector.lanes[element & 7], dynamic_operation);
}

template <unsigned Operation, unsigned Element>
void divide_handler(RspState *state, RspVector *dest, const RspVector *source,
                    const RspVector *target) {
  const auto lane = static_cast<unsigned>(source - state->vectors.data()) & 7;
  execute_divide<Operation, Element>(*state, *dest, lane, *target);
}

template <std::size_t... Index> constexpr auto divide_handlers(std::index_sequence<Index...>) {
  return std::array<VectorHandler, sizeof...(Index)>{
      &divide_handler<0x30 + Index / 16, Index % 16>...};
}

VectorHandler vector_divide_handler(unsigned operation, unsigned element) {
  static constexpr auto table = divide_handlers(std::make_index_sequence<112>{});
  return table[(operation - 0x30) * 16 + element];
}

void Rsp::vector_divide(unsigned operation, unsigned dest, unsigned lane, unsigned source,
                        unsigned element) {
  execute_divide(state_, state_.vectors[dest], lane, state_.vectors[source], operation, element);
}

} // namespace cupid::n64
