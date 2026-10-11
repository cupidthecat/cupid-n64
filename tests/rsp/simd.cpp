#include "core/rsp/vector/execute.hpp"
#include "fixture.hpp"
#include "vector/quantize.hpp"

namespace test {
using namespace cupid::n64;

void rsp_simd_tests() {
  if (!vector_simd_available())
    return;
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
  for (unsigned operation = 0; operation < 64; ++operation)
    for (unsigned element = 0; element < 16; ++element)
      for (unsigned trial = 0; trial < 64; ++trial) {
        RspState actual;
        for (unsigned reg = 0; reg < 32; ++reg)
          for (unsigned lane = 0; lane < 8; ++lane)
            actual.vectors[reg].lanes[lane] = trial < 8
                                                  ? values[(reg + lane + trial) % values.size()]
                                                  : static_cast<std::uint16_t>(next());
        for (unsigned lane = 0; lane < 8; ++lane) {
          const auto product = quantization_products[(lane + trial) % quantization_products.size()];
          actual.accumulator.set(lane,
                                 operation == 11 && trial < quantization_products.size()
                                     ? (std::uint64_t(static_cast<std::uint32_t>(product)) << 16) |
                                           (next() & 0xffff)
                                 : trial < 12 ? accumulators[(lane + trial) % accumulators.size()]
                                              : next());
        }
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
        const bool clipping = operation >= 36 && operation <= 38;
        for (unsigned stage = 0; stage < (clipping ? 3u : 1u); ++stage) {
          const auto current =
              stage == 0 ? instruction : (instruction & ~63u) | (stage == 1 ? 36u : 39u);
          if ((current & 63) >= 48 && (current & 63) <= 54) {
            const auto handler = vector_divide_handler(current & 63, element);
            handler(&expected, &expected.vectors[(current >> 6) & 31],
                    &expected.vectors[(current >> 11) & 31],
                    &expected.vectors[(current >> 16) & 31]);
          } else
            execute_vector_scalar(expected, current);
          equal(execute_vector_simd(actual, current), true);
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
            std::cerr << "vector instruction 0x" << std::hex << current << std::dec << '\n';
            return;
          }
        }
      }
}

} // namespace test
