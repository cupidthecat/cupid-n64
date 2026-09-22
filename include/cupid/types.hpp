#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

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

constexpr u16 byteswap16(u16 value) noexcept {
    return static_cast<u16>((value << 8) | (value >> 8));
}

constexpr u32 byteswap32(u32 value) noexcept {
    return (value << 24) | ((value << 8) & 0x00ff0000U) | ((value >> 8) & 0x0000ff00U) | (value >> 24);
}

constexpr u64 byteswap64(u64 value) noexcept {
    return (static_cast<u64>(byteswap32(static_cast<u32>(value))) << 32) |
           byteswap32(static_cast<u32>(value >> 32));
}

constexpr u16 read_be16(const u8* data) noexcept {
    u16 value = 0;
    if (std::is_constant_evaluated() ||
        (std::endian::native != std::endian::little && std::endian::native != std::endian::big)) {
        value = static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
    } else {
        std::memcpy(&value, data, sizeof(value));
        if constexpr (std::endian::native == std::endian::little)
            value = byteswap16(value);
    }
    return value;
}

constexpr u32 read_be32(const u8* data) noexcept {
    u32 value = 0;
    if (std::is_constant_evaluated() ||
        (std::endian::native != std::endian::little && std::endian::native != std::endian::big)) {
        value = (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
                (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
    } else {
        std::memcpy(&value, data, sizeof(value));
        if constexpr (std::endian::native == std::endian::little)
            value = byteswap32(value);
    }
    return value;
}

constexpr u64 read_be64(const u8* data) noexcept {
    u64 value = 0;
    if (std::is_constant_evaluated() ||
        (std::endian::native != std::endian::little && std::endian::native != std::endian::big)) {
        value = (static_cast<u64>(read_be32(data)) << 32) | read_be32(data + 4);
    } else {
        std::memcpy(&value, data, sizeof(value));
        if constexpr (std::endian::native == std::endian::little)
            value = byteswap64(value);
    }
    return value;
}

constexpr void write_be16(u8* data, u16 value) noexcept {
    if (std::is_constant_evaluated()) {
        data[0] = static_cast<u8>(value >> 8);
        data[1] = static_cast<u8>(value);
        return;
    }
    if constexpr (std::endian::native == std::endian::little)
        value = byteswap16(value);
    else if constexpr (std::endian::native != std::endian::big) {
        data[0] = static_cast<u8>(value >> 8);
        data[1] = static_cast<u8>(value);
        return;
    }
    std::memcpy(data, &value, sizeof(value));
}

constexpr void write_be32(u8* data, u32 value) noexcept {
    if (std::is_constant_evaluated()) {
        data[0] = static_cast<u8>(value >> 24);
        data[1] = static_cast<u8>(value >> 16);
        data[2] = static_cast<u8>(value >> 8);
        data[3] = static_cast<u8>(value);
        return;
    }
    if constexpr (std::endian::native == std::endian::little)
        value = byteswap32(value);
    else if constexpr (std::endian::native != std::endian::big) {
        data[0] = static_cast<u8>(value >> 24);
        data[1] = static_cast<u8>(value >> 16);
        data[2] = static_cast<u8>(value >> 8);
        data[3] = static_cast<u8>(value);
        return;
    }
    std::memcpy(data, &value, sizeof(value));
}

constexpr void write_be64(u8* data, u64 value) noexcept {
    if (std::is_constant_evaluated()) {
        write_be32(data, static_cast<u32>(value >> 32));
        write_be32(data + 4, static_cast<u32>(value));
        return;
    }
    if constexpr (std::endian::native == std::endian::little)
        value = byteswap64(value);
    else if constexpr (std::endian::native != std::endian::big) {
        write_be32(data, static_cast<u32>(value >> 32));
        write_be32(data + 4, static_cast<u32>(value));
        return;
    }
    std::memcpy(data, &value, sizeof(value));
}

} // namespace cupid
