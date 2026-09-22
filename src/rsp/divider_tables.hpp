#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid::rsp {

constexpr std::array<u16, 512> make_reciprocal_table() {
    std::array<u16, 512> table{};
    table[0] = 0xffffU;
    for (u64 index = 1; index < table.size(); ++index) {
        const u64 quotient = (u64{1} << 34) / (index + 512U);
        table[index] = static_cast<u16>((quotient + 1U) >> 8U);
    }
    return table;
}

constexpr u16 reciprocal_sqrt_entry(unsigned index) {
    const u64 a = index < 256U ? index + 256U : 2U * (index - 256U) + 512U;
    u64 low = u64{1} << 17;
    u64 high = u64{1} << 18;
    while (low + 1U < high) {
        const u64 middle = low + (high - low) / 2U;
        if (a * middle * middle < (u64{1} << 44))
            low = middle;
        else
            high = middle;
    }
    return static_cast<u16>(low >> 1U);
}

constexpr std::array<u16, 512> make_reciprocal_sqrt_table() {
    std::array<u16, 512> table{};
    for (unsigned index = 0; index < table.size(); ++index)
        table[index] = reciprocal_sqrt_entry(index);
    return table;
}

inline constexpr auto reciprocal_table = make_reciprocal_table();
inline constexpr auto reciprocal_sqrt_table = make_reciprocal_sqrt_table();

} // namespace cupid::rsp
