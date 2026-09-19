#include "cupid/system.hpp"

#include <algorithm>
#include <bit>

namespace cupid {

u8 Bus::read_ram_byte(u32 address) const {
    return static_cast<u8>(memory.read(address, 1));
}

void Bus::write_ram_byte(u32 address, u8 value) {
    memory.write(address, 1, value);
}

u64 Bus::read_rdram(u32 physical, unsigned width) const {
    if (physical < 0x03f00000U)
        return memory.read(physical, width, (mi_mode_ & 0x100U) != 0);
    if ((mi_mode_ & 0x200U) == 0 && (physical & 4U) != 0)
        return 0;
    const u32 first = memory.read_register(physical);
    if (width == 8)
        return (static_cast<u64>(first) << 32) | memory.read_register(physical + 4);
    return extract_word_lane(first, physical, width);
}

void Bus::write_rdram(u32 physical, unsigned width, u64 value) {
    const bool repeat = (mi_mode_ & 0x80U) != 0;
    mi_mode_ &= ~0x80U;
    const unsigned repeat_length = (mi_mode_ & 0x7fU) + 1U;
    if (physical >= 0x03f00000U) {
        memory.write_register(physical, expand_rcp_write(physical, width, value), repeat ? repeat_length : 0);
        return;
    }
    if (!repeat) {
        memory.write(physical, width, value, (mi_mode_ & 0x100U) != 0);
        return;
    }

    const u64 end = std::min<u64>(static_cast<u64>(physical & ~7U) + repeat_length, rdram.size());
    if (end <= physical)
        return;
    unsigned remaining = static_cast<unsigned>(end - physical);
    if (width != 8) {
        u32 pattern = static_cast<u32>(value);
        if (width == 1) {
            pattern &= 0xffffffffU >> (24 - (physical & 3U) * 8);
            pattern = std::rotr(pattern, 8);
        } else if (width == 2) {
            pattern &= 0xffffffffU >> (16 - (physical & 2U) * 8);
            pattern = std::rotr(pattern, 16);
        }
        value = (static_cast<u64>(pattern) << 32) | pattern;
    }

    const u32 row = physical & ~0x7ffU;
    while (remaining != 0) {
        unsigned transfer = 8;
        while (transfer > remaining || (physical & (transfer - 1)) != 0)
            transfer >>= 1;
        memory.write(physical, transfer, value >> ((8 - transfer) * 8));
        value = std::rotl(value, static_cast<int>(transfer * 8));
        physical = row | ((physical + transfer) & 0x7ffU);
        remaining -= transfer;
    }
}

} // namespace cupid
