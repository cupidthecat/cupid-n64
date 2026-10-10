#pragma once

#include <bit>
#include <cstdint>

namespace cupid::n64 {

class RandomGenerator {
public:
  explicit RandomGenerator(std::uint64_t value = 0, std::uint64_t sequence = 0) {
    seed(value, sequence);
  }

  void seed(std::uint64_t value, std::uint64_t sequence = 0) {
    state_ = 0;
    increment_ = (sequence << 1) | 1;
    step();
    state_ += value;
    step();
  }

  std::uint64_t operator()() {
    const auto upper = step();
    return (std::uint64_t(upper) << 32) | step();
  }

private:
  friend class CoreState;
  std::uint32_t step() {
    const auto value = state_;
    state_ = value * 6364136223846793005ull + increment_;
    const auto bits = static_cast<std::uint32_t>(((value >> 18) ^ value) >> 27);
    return std::rotr(bits, static_cast<int>(value >> 59));
  }

  std::uint64_t state_ = 0;
  std::uint64_t increment_ = 1;
};

} // namespace cupid::n64
