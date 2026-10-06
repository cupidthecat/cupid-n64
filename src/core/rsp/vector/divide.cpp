#include "core/rsp/rsp.hpp"
#include <bit>

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

void Rsp::vector_divide(unsigned operation, unsigned dest, unsigned lane, unsigned source,
                        unsigned element) {
  static const DivideTables tables;
  const auto vector = state_.vectors[source];
  for (unsigned n = 0; n < 8; ++n)
    state_.accumulator.low.lanes[n] = vector.lanes[select_lane(element, n)];
  auto &result = state_.vectors[dest].lanes[lane];
  if (operation == 0x33) {
    result = vector.lanes[select_lane(element, lane)];
    return;
  }
  if (operation == 0x32 || operation == 0x36) {
    state_.divide_double = true;
    state_.divide_input = vector.lanes[element & 7];
    result = state_.divide_output;
    return;
  }
  const bool low = operation == 0x31 || operation == 0x35;
  const auto input =
      low && state_.divide_double
          ? std::bit_cast<std::int32_t>((std::uint32_t(state_.divide_input) << 16) |
                                        vector.lanes[element & 7])
          : static_cast<std::int32_t>(static_cast<std::int16_t>(vector.lanes[element & 7]));
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
    const bool square_root = operation == 0x34 || operation == 0x35;
    value =
        square_root ? tables.square_root[(index & 0x1fe) | (shift & 1)] : tables.reciprocal[index];
    value = ((0x10000 | value) << 14) >> (square_root ? ((31 - shift) >> 1) : (31 - shift));
    value ^= mask;
  }
  state_.divide_double = false;
  state_.divide_output = static_cast<std::uint16_t>(value >> 16);
  result = static_cast<std::uint16_t>(value);
}

} // namespace cupid::n64
