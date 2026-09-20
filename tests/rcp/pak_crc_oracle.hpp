#pragma once

#include "cupid/types.hpp"

#include <span>

namespace test {
using namespace cupid;

// Polynomial long division keeps the oracle independent of the serial CRC loop.
inline u8 address_remainder(u16 address) {
    unsigned remainder = address;
    for (int bit = 15; bit >= 5; --bit)
        if (remainder & (1U << bit))
            remainder ^= 0x35U << (bit - 5);
    return static_cast<u8>(remainder);
}

inline u8 data_remainder(std::span<const u8, 32> data) {
    unsigned remainder = 0;
    for (unsigned byte = 0; byte <= data.size(); ++byte) {
        remainder = (remainder << 8) | (byte == data.size() ? 0 : data[byte]);
        for (int bit = 15; bit >= 8; --bit)
            if (remainder & (1U << bit))
                remainder ^= 0x185U << (bit - 8);
    }
    return static_cast<u8>(remainder);
}

} // namespace test
