#include "../fixture.hpp"
#include "core/state/core_state.hpp"
#include <iostream>
#include <vector>

namespace {
using namespace cupid::n64;

ConsoleConfig config() {
  ConsoleConfig value;
  value.random_seed = 0x1349567;
  value.rtc_clock = [] { return std::int64_t(946684800); };
  value.disk_clock = value.rtc_clock;
  return value;
}

void initialize(Console &console) {
  auto &rsp = console.signal();
  auto &state = rsp.state();
  for (unsigned reg = 0; reg < 32; ++reg)
    for (unsigned lane = 0; lane < 8; ++lane)
      state.vectors[reg].lanes[lane] =
          static_cast<std::uint16_t>((reg * 193 + lane * 7291) ^ 0x8117);
  for (unsigned lane = 0; lane < 8; ++lane)
    state.accumulator.set(lane, 0x800001ff003full + lane * 0x200040011ull);
  state.carry_low = 0xa9;
  state.carry_high = 0x72;
  state.compare_low = 0x3c;
  state.compare_high = 0x69;
  state.extension = 0x5a;
  state.divide_input = 0x8000;
  state.divide_output = 0x0123;
  state.divide_double = true;
  state.gpr[1] = 32;
  for (unsigned n = 0; n < 1024; ++n)
    rsp.write_local(n * 4, 4, (n * 7291) ^ 0x81173456);
  std::vector<std::uint32_t> program;
  for (unsigned element = 0; element < 15; ++element) {
    for (unsigned operation = 0; operation < 64; ++operation) {
      const auto source = 1 + operation % 7;
      const auto target = 1 + (operation + 2) % 7;
      const auto dest = operation % 3 == 0 ? source : operation % 3 == 1 ? target : 0;
      program.push_back(test::vector(operation, dest, source, target, element));
    }
    if (!(element & 3)) {
      program.push_back(0x8c220000);
      program.push_back(0x24430001);
      program.push_back(0xac230004);
      program.push_back(test::vector_memory(false, 4, 8, 1, element, 0));
      program.push_back(test::vector_memory(true, 4, 8, 1, element, 1));
    }
  }
  program.push_back(0x08000000);
  program.push_back(0x25ef0001);
  for (unsigned n = 0; n < program.size(); ++n)
    rsp.write_local(0x1000 + n * 4, 4, program[n]);
  rsp.write_io(16, 1);
}

bool compare(Console &actual, Console &expected, unsigned boundary) {
  const auto difference = CoreState::checkpoint(expected).difference(CoreState::checkpoint(actual));
  test::equal(bool(difference), false);
  if (difference) {
    std::cerr << "vector execution boundary " << boundary << ' ' << difference->field << '['
              << difference->index << "] expected " << std::hex << difference->expected
              << " actual " << difference->actual << std::dec << '\n';
    return false;
  }
  return true;
}

void execution_tests() {
  Console actual(config()), scalar(config()), expected(config());
  scalar.signal().set_vector_execution(Rsp::VectorExecution::Scalar);
  for (auto *console : {&actual, &scalar, &expected})
    initialize(*console);
  constexpr std::array budgets{128u, 4096u, 8191u, 16381u, 65536u, 1543u, 31673u, 7u};
  for (unsigned boundary = 0; boundary < 24; ++boundary) {
    if (boundary == 18) {
      for (auto *console : {&actual, &scalar, &expected})
        console->signal().write_local(0x1000, 4, test::vector(11, 1, 3, 2, 9));
    }
    if (boundary == 4 || boundary == 8 || boundary == 12 || boundary == 16 || boundary == 18 ||
        boundary == 20) {
      actual.signal().set_vector_execution(actual.signal().vector_execution() ==
                                                   Rsp::VectorExecution::Simd
                                               ? Rsp::VectorExecution::Scalar
                                               : Rsp::VectorExecution::Simd);
      scalar.signal().set_vector_execution(scalar.signal().vector_execution() ==
                                                   Rsp::VectorExecution::Simd
                                               ? Rsp::VectorExecution::Scalar
                                               : Rsp::VectorExecution::Simd);
      if (!compare(actual, expected, boundary) || !compare(scalar, expected, boundary))
        return;
    }
    if (boundary == 12) {
      const auto saved = CoreState::capture(expected);
      for (auto *console : {&actual, &scalar, &expected})
        CoreState::restore(*console, saved);
      test::equal(actual.signal().vector_execution() == Rsp::VectorExecution::Scalar, true);
      test::equal(scalar.signal().vector_execution() == Rsp::VectorExecution::Simd, true);
    }
    for (auto *console : {&actual, &scalar, &expected})
      console->signal().advance(budgets[boundary % budgets.size()]);
    if (!compare(actual, expected, boundary) || !compare(scalar, expected, boundary))
      return;
  }
  test::equal(actual.signal().state().gpr[15] != 0, true);
}
} // namespace

int main() {
  execution_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
