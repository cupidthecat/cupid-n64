#pragma once

#include "cupid/types.hpp"

#include <bit>
#include <optional>

namespace cupid::fpu_host {

struct IntegerConversionResult {
    u64 bits{};
    bool inexact{};
};

template <unsigned FractionBits, unsigned ExponentBits, unsigned Bias>
IntegerConversionResult bits_from_integer(s64 value, unsigned rounding) {
    const bool negative = value < 0;
    const u64 encoded = std::bit_cast<u64>(value);
    const u64 magnitude = negative ? 0U - encoded : encoded;
    if (magnitude == 0)
        return {};

    unsigned exponent = static_cast<unsigned>(std::bit_width(magnitude)) - 1U;
    u64 significand = magnitude;
    bool inexact = false;
    if (exponent <= FractionBits) {
        significand <<= FractionBits - exponent;
    } else {
        const unsigned shift = exponent - FractionBits;
        const u64 remainder = magnitude & ((1ULL << shift) - 1U);
        const u64 half = 1ULL << (shift - 1U);
        significand >>= shift;
        inexact = remainder != 0;
        if ((rounding == 0 && (remainder > half || (remainder == half && (significand & 1U) != 0))) ||
            (rounding == 2 && !negative && inexact) || (rounding == 3 && negative && inexact)) {
            ++significand;
            if (significand == (1ULL << (FractionBits + 1U))) {
                significand >>= 1U;
                ++exponent;
            }
        }
    }
    constexpr u64 fraction_mask = (1ULL << FractionBits) - 1U;
    const u64 sign = static_cast<u64>(negative) << (FractionBits + ExponentBits);
    return {sign | (static_cast<u64>(exponent + Bias) << FractionBits) | (significand & fraction_mask),
            inexact};
}

// Decode and round without changing the host floating-point environment.
// Unsupported inputs and results stay on the ordinary exception path.
template <unsigned FractionBits, unsigned ExponentBits, unsigned Bias>
std::optional<IntegerConversionResult> integer_from_bits(u64 bits, bool long_result, unsigned rounding) {
    constexpr u64 fraction_mask = (1ULL << FractionBits) - 1U;
    constexpr u64 exponent_mask = (1ULL << ExponentBits) - 1U;
    const bool negative = ((bits >> (FractionBits + ExponentBits)) & 1U) != 0;
    const u64 fraction = bits & fraction_mask;
    const u64 encoded_exponent = (bits >> FractionBits) & exponent_mask;
    if (encoded_exponent == 0)
        return fraction == 0 ? std::optional<IntegerConversionResult>{{0, false}} : std::nullopt;
    if (encoded_exponent == exponent_mask)
        return std::nullopt;

    const int exponent = static_cast<int>(encoded_exponent) - static_cast<int>(Bias);
    if (long_result ? exponent >= 53 : exponent > 31 || (exponent == 31 && (!negative || fraction != 0)))
        return std::nullopt;

    const u64 significand = (1ULL << FractionBits) | fraction;
    u64 magnitude = 0;
    bool inexact = true;
    bool above_half = false;
    bool tie = false;
    if (exponent == -1) {
        above_half = fraction != 0;
        tie = fraction == 0;
    } else if (exponent >= static_cast<int>(FractionBits)) {
        magnitude = significand << (static_cast<unsigned>(exponent) - FractionBits);
        inexact = false;
    } else if (exponent >= 0) {
        const unsigned shift = FractionBits - static_cast<unsigned>(exponent);
        magnitude = significand >> shift;
        const u64 remainder = significand & ((1ULL << shift) - 1U);
        const u64 half = 1ULL << (shift - 1U);
        inexact = remainder != 0;
        above_half = remainder > half;
        tie = remainder == half;
    }

    if ((rounding == 0 && (above_half || (tie && (magnitude & 1U) != 0))) ||
        (rounding == 2 && !negative && inexact) || (rounding == 3 && negative && inexact))
        ++magnitude;
    if (!long_result && magnitude > (negative ? 0x80000000ULL : 0x7fffffffULL))
        return std::nullopt;
    return IntegerConversionResult{negative ? 0U - magnitude : magnitude, inexact};
}

} // namespace cupid::fpu_host
