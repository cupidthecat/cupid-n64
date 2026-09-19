#pragma once

#include <bit>
#include <cstdint>

namespace cupid {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

constexpr u64 sign_extend32(u32 value) noexcept {
    return static_cast<u64>(static_cast<s64>(std::bit_cast<s32>(value)));
}

constexpr u64 sign_extend16(u16 value) noexcept {
    return static_cast<u64>(static_cast<s64>(std::bit_cast<s16>(value)));
}

constexpr u64 sign_extend8(u8 value) noexcept {
    return static_cast<u64>(static_cast<s64>(std::bit_cast<s8>(value)));
}

constexpr s64 signed64(u64 value) noexcept {
    return std::bit_cast<s64>(value);
}
constexpr s32 signed32(u32 value) noexcept {
    return std::bit_cast<s32>(value);
}

constexpr u32 read_be32(const u8* data) noexcept {
    return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
           (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
}

constexpr void write_be32(u8* data, u32 value) noexcept {
    data[0] = static_cast<u8>(value >> 24);
    data[1] = static_cast<u8>(value >> 16);
    data[2] = static_cast<u8>(value >> 8);
    data[3] = static_cast<u8>(value);
}

} // namespace cupid
