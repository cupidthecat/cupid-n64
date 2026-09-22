#include "cupid/rdp/color_pipeline.hpp"

#include <array>
#include <utility>

namespace cupid {
namespace {

constexpr u8 divide(unsigned numerator, unsigned denominator) {
    // Only three remainder bits survive between quotient stages.
    unsigned remainder = (16U - denominator + (numerator >> 8U)) & 7U;
    unsigned quotient = 0;
    unsigned previous = 0;
    for (unsigned stage = 0; stage < 8; ++stage) {
        const unsigned addend = previous != 0 ? 16U - denominator : denominator;
        const unsigned sum = remainder * 2U + ((numerator >> (7U - stage)) & 1U) + addend;
        previous = (sum >> 4U) & 1U;
        quotient = quotient * 2U + previous;
        remainder = sum & 7U;
    }
    return static_cast<u8>(quotient);
}

template <unsigned Block> constexpr std::array<u8, 256> make_block() {
    std::array<u8, 256> values{};
    for (unsigned low = 0; low < values.size(); ++low)
        values[low] = divide((Block % 8U) * 256U + low, Block / 8U);
    return values;
}

// Separate constant evaluations keep table generation within compiler limits.
template <unsigned Block> constexpr auto block = make_block<Block>();

template <unsigned... Blocks> constexpr auto block_pointers(std::integer_sequence<unsigned, Blocks...>) {
    return std::array<const u8*, sizeof...(Blocks)>{block<Blocks>.data()...};
}

constexpr auto table = block_pointers(std::make_integer_sequence<unsigned, 128>{});

} // namespace

u8 rdp_blend_divide(unsigned numerator, unsigned denominator) {
    const unsigned index = ((denominator & 15U) << 3U) | ((numerator >> 8U) & 7U);
    return table[index][numerator & 255U];
}

} // namespace cupid
