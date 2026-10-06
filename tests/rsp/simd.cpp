#include "core/rsp/vector/execute.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_simd_tests() {
  if (!vector_simd_available())
    return;
  constexpr std::array operations{0u,  1u,  4u,  5u,  6u,  7u,  8u,  9u,  12u, 13u, 14u,
                                  15u, 16u, 17u, 19u, 20u, 21u, 29u, 32u, 33u, 34u, 35u,
                                  39u, 40u, 41u, 42u, 43u, 44u, 45u, 55u, 63u};
  constexpr std::array<std::uint16_t, 8> values{0,      1,      0x7fff, 0x8000,
                                                0xffff, 0x7ffe, 0x8001, 0xfffe};
  constexpr std::array<std::uint64_t, 12> accumulators{
      0,          1,          0x7fff,         0x8000,         0xffff,         0x7fffffff,
      0x80000000, 0xffffffff, 0x7fffffffffff, 0x800000000000, 0xffffffffffff, 0xffff80000000};
  std::uint64_t random = 0x8914354723489ull;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return random;
  };
  for (auto operation : operations)
    for (unsigned element = 0; element < 16; ++element)
      for (unsigned trial = 0; trial < 64; ++trial) {
        RspState actual;
        for (unsigned reg = 0; reg < 32; ++reg)
          for (unsigned lane = 0; lane < 8; ++lane)
            actual.vectors[reg].lanes[lane] = trial < 8
                                                  ? values[(reg + lane + trial) % values.size()]
                                                  : static_cast<std::uint16_t>(next());
        for (unsigned lane = 0; lane < 8; ++lane)
          actual.accumulator.set(
              lane, trial < 12 ? accumulators[(lane + trial) % accumulators.size()] : next());
        actual.carry_low = static_cast<std::uint8_t>(next());
        actual.carry_high = static_cast<std::uint8_t>(next());
        actual.compare_low = static_cast<std::uint8_t>(next());
        actual.compare_high = static_cast<std::uint8_t>(next());
        actual.extension = static_cast<std::uint8_t>(next());
        actual.divide_input = static_cast<std::uint16_t>(next());
        actual.divide_output = static_cast<std::uint16_t>(next());
        actual.divide_double = next() & 1;
        auto expected = actual;
        const auto instruction =
            vector(operation, static_cast<unsigned>(next() & 31),
                   static_cast<unsigned>(next() & 31), static_cast<unsigned>(next() & 31), element);
        const auto before = failures;
        execute_vector_scalar(expected, instruction);
        equal(execute_vector_simd(actual, instruction), true);
        for (unsigned reg = 0; reg < 32; ++reg)
          for (unsigned lane = 0; lane < 8; ++lane)
            equal(actual.vectors[reg].lanes[lane], expected.vectors[reg].lanes[lane]);
        for (unsigned lane = 0; lane < 8; ++lane)
          equal(actual.accumulator.get(lane), expected.accumulator.get(lane));
        equal(actual.carry_low, expected.carry_low);
        equal(actual.carry_high, expected.carry_high);
        equal(actual.compare_low, expected.compare_low);
        equal(actual.compare_high, expected.compare_high);
        equal(actual.extension, expected.extension);
        equal(actual.divide_input, expected.divide_input);
        equal(actual.divide_output, expected.divide_output);
        equal(actual.divide_double, expected.divide_double);
        if (failures != before) {
          std::cerr << "vector instruction 0x" << std::hex << instruction << std::dec << '\n';
          return;
        }
      }
}

} // namespace test
