#include "../fixture.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;
namespace {

std::array<std::uint64_t, 8> sample(Console &console) {
  std::array<std::uint64_t, 8> result{};
  console.cpu().write_control(Wired, 63);
  console.signal().write_io(16, 1);
  for (unsigned n = 0; n < result.size(); n += 2) {
    result[n] = console.cpu().read_control(Random);
    result[n + 1] = console.signal().read_status(0);
  }
  console.controller(0).memory_pak();
  return result;
}

void compare(Console &actual, Console &expected) {
  const auto a = sample(actual);
  const auto b = sample(expected);
  for (unsigned n = 0; n < a.size(); ++n)
    equal(a[n], b[n]);
  const auto a_pak = actual.controller(0).pak_data();
  const auto b_pak = expected.controller(0).pak_data();
  equal(a_pak.size(), b_pak.size());
  for (unsigned n = 0; n < a_pak.size(); ++n)
    equal(a_pak[n], b_pak[n]);
}

} // namespace

void entropy_tests() {
  struct Case {
    std::uint64_t seed;
    bool expansion;
    std::array<std::uint64_t, 8> values;
  };
  constexpr Case cases[]{
      {0ull, false, {13, 2619, 36, 3604, 27, 3074, 40, 1769}},
      {0ull, true, {27, 3074, 40, 1769, 33, 32, 34, 3289}},
      {1311768467463790320ull, false, {14, 2372, 2, 3086, 1, 2713, 13, 2598}},
      {1311768467463790320ull, true, {1, 2713, 13, 2598, 20, 24, 52, 3721}},
      {18446744073709551615ull, false, {9, 3289, 28, 1795, 25, 417, 55, 805}},
      {18446744073709551615ull, true, {25, 417, 55, 805, 19, 938, 44, 3521}},
  };
  for (const auto &entry : cases) {
    ConsoleConfig config;
    config.expansion = entry.expansion;
    config.random_seed = entry.seed;
    auto machine = std::make_unique<Console>(config);
    const auto actual = sample(*machine);
    for (unsigned n = 0; n < actual.size(); ++n)
      equal(actual[n], entry.values[n]);
  }

  for (bool expansion : {false, true}) {
    unsigned clock_reads = 0;
    constexpr std::uint64_t seeds[]{0x123456789abcdef0, 0x100000000, 0};
    ConsoleConfig normal;
    normal.expansion = expansion;
    normal.entropy_clock = [&] { return seeds[clock_reads++ % 3]; };
    auto machine = std::make_unique<Console>(normal);
    equal(clock_reads, 1);
    ConsoleConfig fixed;
    fixed.expansion = expansion;
    fixed.random_seed = seeds[0];
    auto expected = std::make_unique<Console>(fixed);
    compare(*machine, *expected);
    for (unsigned n = 0; n < 3; ++n) {
      machine->power(true);
      expected->power(true);
      equal(clock_reads, 1);
      compare(*machine, *expected);
    }
    machine->power();
    equal(clock_reads, 2);
    fixed.random_seed = seeds[1];
    expected = std::make_unique<Console>(fixed);
    compare(*machine, *expected);

    std::vector<std::uint8_t> rom(4096);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    std::array<std::uint8_t, 0x7c0> firmware{};
    equal(machine->load(rom, firmware), true);
    equal(clock_reads, 3);
    fixed.random_seed = seeds[2];
    expected = std::make_unique<Console>(fixed);
    compare(*machine, *expected);
  }

  for (auto seed : {0ull, 1ull, 0x100000000ull, 0x8000000000000000ull, 0xdeadbeef12345678ull,
                    0xffffffffffffffffull}) {
    unsigned clock_reads = 0;
    ConsoleConfig fixed;
    fixed.random_seed = seed;
    fixed.entropy_clock = [&] {
      ++clock_reads;
      return seed ^ 0xffffffffffffffffull;
    };
    auto machine = std::make_unique<Console>(fixed);
    const auto first = sample(*machine);
    machine->power();
    const auto second = sample(*machine);
    for (unsigned n = 0; n < first.size(); ++n)
      equal(second[n], first[n]);
    equal(clock_reads, 0);
  }
}

} // namespace test
