#include "core/rsp/vector/execute.hpp"
#include "fixture.hpp"
#include <vector>

namespace test {
using namespace cupid::n64;
namespace {

void compare_vectors(const RspState &actual, const RspState &expected) {
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(actual.gpr[reg], expected.gpr[reg]);
    for (unsigned lane = 0; lane < 8; ++lane)
      equal(actual.vectors[reg].lanes[lane], expected.vectors[reg].lanes[lane]);
  }
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
}

} // namespace

void rsp_native_vector_tests() {
  RspFixture actual;
  RspFixture expected;
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
      for (unsigned trial = 0;
           trial < (operation == 29 || (operation >= 40 && operation <= 45) ? 128u : 64u);
           ++trial) {
        const auto sample = trial & 63;
        actual.rsp.power();
        expected.rsp.power();
        auto &state = actual.rsp.state();
        for (unsigned reg = 0; reg < 32; ++reg) {
          state.gpr[reg] = reg ? static_cast<std::uint32_t>(next()) : 0;
          for (unsigned lane = 0; lane < 8; ++lane)
            state.vectors[reg].lanes[lane] = sample < 16
                                                 ? values[(reg + lane + sample) % values.size()]
                                                 : static_cast<std::uint16_t>(next());
        }
        for (unsigned lane = 0; lane < 8; ++lane)
          state.accumulator.set(
              lane, sample < 24 ? accumulators[(lane + sample) % accumulators.size()] : next());
        state.carry_low = static_cast<std::uint8_t>(next());
        state.carry_high = static_cast<std::uint8_t>(next());
        state.compare_low = static_cast<std::uint8_t>(next());
        state.compare_high = static_cast<std::uint8_t>(next());
        state.extension = static_cast<std::uint8_t>(next());
        state.divide_input = static_cast<std::uint16_t>(next());
        state.divide_output = static_cast<std::uint16_t>(next());
        state.divide_double = trial & 1;
        expected.rsp.state() = state;
        const auto source = trial & 31;
        const auto target = trial % 5 >= 3 ? source : (source + 7) & 31;
        const auto dest = trial % 5 == 1 || trial % 5 == 4 ? source
                          : trial % 5 == 2                 ? target
                                                           : (source + 13) & 31;
        const auto instruction = vector(operation, dest, source, target, element);
        std::vector<std::uint32_t> instructions{instruction};
        if (trial >= 64) {
          instructions.push_back(vector(29, target, dest, source, 10));
          instructions.push_back(vector(0, dest, dest, target, element));
          instructions.push_back(vector(45, dest, dest, target, element));
          instructions.push_back(vector(29, source, dest, target, 9));
        }
        instructions.push_back(vector(63, 0, 0, 0));
        for (auto *fixture : {&actual, &expected}) {
          for (unsigned n = 0; n < instructions.size(); ++n)
            fixture->rsp.write_local(0x1000 + n * 4, 4, instructions[n]);
          fixture->rsp.write_local(0x1000 + static_cast<std::uint32_t>(instructions.size()) * 4, 4,
                                   13);
          fixture->rsp.write_io(16, 1);
        }
        auto scalar = state;
        for (const auto opcode : instructions) {
          const auto op = opcode & 63;
          const auto e = (opcode >> 21) & 15;
          const auto handler = op >= 0x30 && op <= 0x36 ? vector_divide_handler(op, e)
                                                        : vector_scalar_handler(op, e);
          handler(&scalar, &scalar.vectors[(opcode >> 6) & 31],
                  &scalar.vectors[(opcode >> 11) & 31], &scalar.vectors[(opcode >> 16) & 31]);
        }
        actual.rsp.advance(1);
        unsigned clocks = 0;
        while (!expected.rsp.status().halted)
          clocks += expected.rsp.step();
        const auto before = failures;
        equal(actual.rsp.clocks(), clocks - 1);
        equal(actual.rsp.pc(), expected.rsp.pc());
        equal(actual.rsp.status().broken, true);
        compare_vectors(state, expected.rsp.state());
        compare_vectors(scalar, expected.rsp.state());
        if (failures != before) {
          std::cerr << "native vector operation=" << operation << " element=" << element
                    << " trial=" << trial << " source=" << source << " target=" << target
                    << " dest=" << dest << '\n';
          return;
        }
      }
}

} // namespace test
