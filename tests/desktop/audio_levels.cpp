#include "desktop/audio/levels.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0, failures = 0;
void check(bool condition) {
  ++checks;
  failures += !condition;
}
} // namespace

int main() {
  using namespace cupid::desktop;
  const std::array<std::int16_t, 8> source{-32768, 32767, -10000, 10000, -1, 1, 0, 0};
  const auto original = source;
  std::array<std::int16_t, 8> output{};
  render_audio(source, output, {});
  check(output == source);
  render_audio(source, output, {0, 0});
  check(output == decltype(output){});
  render_audio(source, output, {50, 0});
  check(output == decltype(output){-16384, 16384, -5000, 5000, -1, 1, 0, 0});
  render_audio(source, output, {100, -100});
  check(output == decltype(output){-32768, 0, -10000, 0, -1, 0, 0, 0});
  render_audio(source, output, {100, 100});
  check(output == decltype(output){0, 32767, 0, 10000, 0, 1, 0, 0});
  render_audio(source, output, {100, -50});
  check(output == decltype(output){-32768, 16384, -10000, 5000, -1, 1, 0, 0});
  render_audio(source, output, {200, 0});
  check(output == decltype(output){-32768, 32767, -20000, 20000, -2, 2, 0, 0});
  check(source == original);
  const auto rejected = [&](auto input, auto destination, AudioLevels levels) {
    bool failed = false;
    try {
      render_audio(input, destination, levels);
    } catch (const std::invalid_argument &) {
      failed = true;
    }
    check(failed);
  };
  rejected(std::span(source).first(3), std::span(output).first(3), {});
  rejected(std::span(source), std::span(output).first(6), {});
  rejected(std::span(source), std::span(output), {201, 0});
  rejected(std::span(source), std::span(output), {100, -101});
  rejected(std::span(source), std::span(output), {100, 101});
  render_audio({}, {}, {});
  check(source == original);
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
