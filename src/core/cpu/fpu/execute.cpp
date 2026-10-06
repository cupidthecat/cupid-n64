#include "core/cpu/cpu.hpp"
#include "core/cpu/fpu/environment.hpp"
#include <cmath>
#include <limits>
#include <type_traits>

namespace cupid::n64 {
namespace {

template <typename Float>
using Bits = std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;

template <typename Float> struct Representation {
  static constexpr Bits<Float> sign = Bits<Float>(1) << (sizeof(Float) * 8 - 1);
  static constexpr Bits<Float> fraction =
      sizeof(Float) == 4 ? 0x007fffffull : 0x000fffffffffffffull;
  static constexpr Bits<Float> exponent =
      sizeof(Float) == 4 ? 0x7f800000ull : 0x7ff0000000000000ull;
  static constexpr Bits<Float> signaling =
      sizeof(Float) == 4 ? 0x00400000ull : 0x0008000000000000ull;
  static constexpr Bits<Float> canonical =
      sizeof(Float) == 4 ? 0x7fbfffffull : 0x7ff7ffffffffffffull;

  Bits<Float> bits;
  explicit Representation(Float value) : bits(std::bit_cast<Bits<Float>>(value)) {}
  bool nan() const {
    return (bits & exponent) == exponent && (bits & fraction);
  }
  bool infinity() const {
    return (bits & ~sign) == exponent;
  }
  bool subnormal() const {
    return !(bits & exponent) && (bits & fraction);
  }
  bool signals() const {
    return bits & signaling;
  }
};

template <typename Float> Float register_value(std::uint64_t value) {
  return std::bit_cast<Float>(static_cast<Bits<Float>>(value));
}

template <typename Float> std::uint64_t result_bits(Float value) {
  const Representation<Float> repr(value);
  return repr.nan() ? Representation<Float>::canonical : repr.bits;
}

template <typename Float> Float calculate(unsigned operation, Float a, Float b) {
  volatile Float left = a;
  volatile Float right = b;
  switch (operation) {
  case 0:
    return left + right;
  case 1:
    return left - right;
  case 2:
    return left * right;
  case 3:
    return left / right;
  case 4:
    return std::sqrt(left);
  default:
    return -left;
  }
}

} // namespace

bool Cpu::fpu_unimplemented() {
  state_.fcr31 |= 0x20000;
  raise(Exception::FloatingPoint);
  return false;
}

bool Cpu::fpu_exception(unsigned exceptions) {
  const auto enabled = (state_.fcr31 >> 7) & 31;
  state_.fcr31 |= (exceptions & 31) << 12;
  state_.fcr31 |= (exceptions & ~enabled & 31) << 2;
  if (!(exceptions & enabled))
    return false;
  raise(Exception::FloatingPoint);
  return true;
}

bool Cpu::fpu_host_exceptions(unsigned exceptions, bool conversion) {
  if (conversion && (exceptions & floating::Invalid)) {
    fpu_unimplemented();
    return true;
  }
  if ((exceptions & floating::Underflow) &&
      (!(state_.fcr31 & 0x01000000) || (state_.fcr31 & 0x180))) {
    fpu_unimplemented();
    return true;
  }
  return fpu_exception(exceptions);
}

template <typename Float> bool Cpu::fpu_inputs(Float a, std::optional<Float> b) {
  const Representation<Float> first(a);
  const Representation<Float> second(b.value_or(a));
  if ((first.nan() && !first.signals()) || (second.nan() && !second.signals()) ||
      first.subnormal() || second.subnormal())
    return fpu_unimplemented();
  if ((first.nan() || second.nan()) && fpu_exception(floating::Invalid))
    return false;
  return true;
}

template <typename Float> bool Cpu::fpu_output(Float &value) {
  const Representation<Float> repr(value);
  if (repr.subnormal()) {
    if (!(state_.fcr31 & 0x01000000) || (state_.fcr31 & 0x180))
      return fpu_unimplemented();
    fpu_exception(floating::Underflow | floating::Inexact);
    auto bits = repr.bits & Representation<Float>::sign;
    const auto rounding = state_.fcr31 & 3;
    if ((rounding == 2 && !bits) || (rounding == 3 && bits))
      bits |= std::bit_cast<Bits<Float>>(std::numeric_limits<Float>::min());
    value = std::bit_cast<Float>(bits);
  }
  return true;
}

template <typename Float>
void Cpu::fpu_to_integer(unsigned dest, unsigned source, bool wide, unsigned rounding) {
  const auto value = register_value<Float>(state_.fpr[fpu_source(source)]);
  const Representation<Float> repr(value);
  const auto limit = wide ? Float(0x1p53) : Float(0x1p31);
  if (repr.subnormal() || repr.nan() || repr.infinity() || value >= limit ||
      (wide ? value <= -limit : value < -limit)) {
    fpu_unimplemented();
    return;
  }
  floating::Environment environment(rounding < 4 ? rounding : state_.fcr31 & 3);
  volatile Float input = value;
  volatile Float rounded = std::nearbyint(input);
  if (!wide && (rounded >= Float(0x1p31) || rounded < -Float(0x1p31))) {
    fpu_unimplemented();
    return;
  }
  const auto result = static_cast<std::int64_t>(rounded);
  auto exceptions = environment.exceptions();
  if (rounded != value)
    exceptions |= floating::Inexact;
  if (fpu_host_exceptions(exceptions, !wide))
    return;
  state_.fpr[dest] = wide ? static_cast<std::uint64_t>(result) : static_cast<std::uint32_t>(result);
  advance_clocks(8);
}

template <typename Float> void Cpu::fpu_convert(unsigned dest, unsigned source, unsigned format) {
  const auto bits = state_.fpr[fpu_source(source)];
  floating::Environment environment(state_.fcr31 & 3);
  Float result;
  unsigned clocks = 0;
  if (format == 20 || format == 21) {
    const auto value =
        format == 20 ? std::int64_t(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)))
                     : signed_value(bits);
    if (format == 21 && (value >= 0x0080000000000000ll || value < -0x0080000000000000ll)) {
      fpu_unimplemented();
      return;
    }
    volatile std::int64_t input = value;
    volatile Float converted = static_cast<Float>(input);
    result = converted;
    clocks = 8;
  } else if constexpr (sizeof(Float) == 4) {
    if (format != 17) {
      fpu_unimplemented();
      return;
    }
    const auto value = register_value<double>(bits);
    if (!fpu_inputs(value))
      return;
    volatile double input = value;
    volatile Float converted = static_cast<Float>(input);
    result = converted;
    clocks = 2;
  } else {
    if (format != 16) {
      fpu_unimplemented();
      return;
    }
    const auto value = register_value<float>(bits);
    if (!fpu_inputs(value))
      return;
    volatile float input = value;
    volatile Float converted = input;
    result = converted;
  }
  if (fpu_host_exceptions(environment.exceptions()) || !fpu_output(result))
    return;
  state_.fpr[dest] = result_bits(result);
  advance_clocks(clocks);
}

template <typename Float> void Cpu::fpu_arithmetic(std::uint32_t instruction) {
  const auto operation = instruction & 63;
  const auto dest = (instruction >> 6) & 31;
  const auto source = (instruction >> 11) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto source_bits = state_.fpr[fpu_source(source)];
  if (operation == 6) {
    state_.fpr[dest] = source_bits;
    return;
  }
  if (!fpu_begin())
    return;
  const auto a = register_value<Float>(source_bits);
  const auto b = register_value<Float>(state_.fpr[target]);
  if (operation >= 0x30) {
    const Representation<Float> first(a), second(b);
    const bool unordered = first.nan() || second.nan();
    if (unordered && ((operation & 8) || (first.nan() && first.signals()) ||
                      (second.nan() && second.signals()))) {
      if (fpu_exception(floating::Invalid))
        return;
    }
    floating::Environment environment(state_.fcr31 & 3);
    const bool condition =
        unordered ? operation & 1 : ((operation & 4) && a < b) || ((operation & 2) && a == b);
    state_.fcr31 = (state_.fcr31 & ~0x800000u) | (condition ? 0x800000u : 0);
    return;
  }
  if (operation >= 8 && operation <= 15)
    return fpu_to_integer<Float>(dest, source, operation < 12, operation & 3);
  if (operation == 0x24 || operation == 0x25)
    return fpu_to_integer<Float>(dest, source, operation == 0x25, 4);
  if (operation == 0x20 || operation == 0x21) {
    const auto format = sizeof(Float) == 4 ? 16u : 17u;
    if (operation == 0x20)
      return fpu_convert<float>(dest, source, format);
    return fpu_convert<double>(dest, source, format);
  }
  if (operation > 7) {
    fpu_unimplemented();
    return;
  }
  if (!fpu_inputs(a, operation <= 3 ? std::optional<Float>(b) : std::nullopt))
    return;
  Float result;
  if (operation == 5) {
    result = std::bit_cast<Float>(std::bit_cast<Bits<Float>>(a) & ~Representation<Float>::sign);
  } else {
    floating::Environment environment(state_.fcr31 & 3);
    volatile Float calculated = calculate(operation, a, b);
    result = calculated;
    if (fpu_host_exceptions(environment.exceptions()))
      return;
  }
  if (!fpu_output(result))
    return;
  state_.fpr[dest] = result_bits(result);
  unsigned clocks = 0;
  if (operation <= 1)
    clocks = 4;
  else if (operation == 2)
    clocks = sizeof(Float) == 4 ? 8 : 14;
  else if (operation == 3 || operation == 4)
    clocks = sizeof(Float) == 4 ? 56 : 114;
  advance_clocks(clocks);
}

template void Cpu::fpu_arithmetic<float>(std::uint32_t);
template void Cpu::fpu_arithmetic<double>(std::uint32_t);
template void Cpu::fpu_convert<float>(unsigned, unsigned, unsigned);
template void Cpu::fpu_convert<double>(unsigned, unsigned, unsigned);

} // namespace cupid::n64
