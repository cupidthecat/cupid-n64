#include "cupid/system.hpp"

#include <algorithm>

namespace cupid {

void Cpu::address_exception(u64 address, Access access) {
    cp0[8] = address;
    cp0[10] = (cp0[10] & 255U) | (address & 0xc00000ffffffe000ULL);
    cp0[4] = (cp0[4] & ~0x7fffffULL) | ((address >> 9) & 0x7ffff0U);
    cp0[20] =
        (cp0[20] & ~0x1ffffffffULL) | ((address >> 9) & 0x7ffffff0U) | ((address >> 31) & 0x180000000ULL);
    raise_exception(access == Access::Write ? Exception::AddressStore : Exception::AddressLoad, 0, false,
                    access == Access::Execute);
}

void Cpu::tlb_exception(u64 address, Access access, bool refill, bool modification) {
    cp0[8] = address;
    cp0[10] = (cp0[10] & 255U) | (address & 0xc00000ffffffe000ULL);
    cp0[4] = (cp0[4] & ~0x7fffffULL) | ((address >> 9) & 0x7ffff0U);
    cp0[20] =
        (cp0[20] & ~0x1ffffffffULL) | ((address >> 9) & 0x7ffffff0U) | ((address >> 31) & 0x180000000ULL);
    raise_exception(modification              ? Exception::TlbModification
                    : access == Access::Write ? Exception::TlbStore
                                              : Exception::TlbLoad,
                    0, refill, access == Access::Execute);
}

bool Cpu::translate(u64 address, Access access, u32& physical, bool& cached) {
    const bool kernel = kernel_mode();
    const bool supervisor = !kernel && (status() & 0x18U) == 8U;
    const bool wide = wide_addressing();
    const u32 low = static_cast<u32>(address);
    const bool compatible = address == sign_extend32(low);
    bool mapped = false;

    if (!wide && !compatible) {
        address_exception(address, access);
        return false;
    }
    if (compatible) {
        if (low < 0x80000000U)
            mapped = true;
        else if (kernel && low < 0xc0000000U) {
            physical = low & 0x1fffffffU;
            cached = low < 0xa0000000U;
            return true;
        } else if ((kernel || supervisor) && low >= 0xc0000000U && low < 0xe0000000U)
            mapped = true;
        else if (kernel && low >= 0xe0000000U)
            mapped = true;
    } else if (wide) {
        const u64 region = address >> 62;
        if (region == 0 && address <= 0x000000ffffffffffULL)
            mapped = true;
        else if (region == 1 && (kernel || supervisor) && address <= 0x400000ffffffffffULL)
            mapped = true;
        else if (region == 2 && kernel && (address & 0x07ffffff00000000ULL) == 0) {
            physical = low;
            cached = ((address >> 59) & 7U) != 2;
            return true;
        } else if (region == 3 && kernel && address <= 0xc00000ff7fffffffULL)
            mapped = true;
    }
    if (!mapped) {
        address_exception(address, access);
        return false;
    }

    for (const auto& entry : tlb) {
        const u64 match_mask = 0xc00000ffffffe000ULL & ~static_cast<u64>(entry.page_mask);
        if ((address & match_mask) != (entry.entry_hi & match_mask))
            continue;
        if (!entry.global && (entry.entry_hi & 255U) != (cp0[10] & 255U))
            continue;
        const u32 offset_mask = (entry.page_mask | 0x1fffU) >> 1;
        const unsigned page = (address & (static_cast<u64>(offset_mask) + 1)) != 0 ? 1U : 0U;
        const u32 lo_entry = entry.entry_lo[page];
        if ((lo_entry & 2U) == 0) {
            tlb_exception(address, access, false, false);
            return false;
        }
        if (access == Access::Write && (lo_entry & 4U) == 0) {
            tlb_exception(address, access, false, true);
            return false;
        }
        physical = ((lo_entry >> 6) << 12) + (low & offset_mask);
        cached = ((lo_entry >> 3) & 7U) != 2;
        return true;
    }
    tlb_exception(address, access, true, false);
    return false;
}

bool Cpu::writeback(CacheLine<16>& line, unsigned index) {
    const u32 base = line.tag | ((index << 4) & 0xff0U);
    add_cycles(40);
    return system_.bus.write_cache(base, line.data);
}

bool Cpu::fill_data_cache(CacheLine<16>& line, u32 physical, unsigned index) {
    if (line.valid && line.dirty && !writeback(line, index))
        return false;
    line.tag = physical & 0xfffff000U;
    const u32 base = line.tag | ((index << 4) & 0xff0U);
    if (!system_.bus.read_cache(base, line.data))
        return false;
    line.valid = true;
    line.dirty = false;
    add_cycles(40);
    return true;
}

bool Cpu::read_memory(u64 address, unsigned width, u64& value, bool instruction) {
    const Access access = instruction ? Access::Execute : Access::Read;
    if ((width != 1 && width != 2 && width != 4 && width != 8) || (address & (width - 1)) != 0) {
        address_exception(address, access);
        return false;
    }
    u32 physical = 0;
    bool cached = false;
    if (!translate(address, access, physical, cached))
        return false;
    if (physical >= 0x80000000U || (width == 8 && physical >= 0x04000000U)) {
        frozen = true;
        return false;
    }
    if (little_endian())
        physical ^= 8U - width;
    if (!cached) {
        value = system_.bus.read(physical, width);
        add_cycles(4);
        return !frozen;
    }
    if (instruction) {
        const unsigned index = static_cast<unsigned>((address >> 5) & 511U);
        auto& line = instruction_cache[index];
        if (!line.valid || line.tag != (physical & 0xfffff000U)) {
            line.tag = physical & 0xfffff000U;
            const u32 base = line.tag | ((index << 5) & 0xfe0U);
            if (!system_.bus.read_cache(base, line.data))
                return false;
            line.valid = true;
            add_cycles(48);
        }
        value = read_be32(line.data.data() + (physical & 28U));
        return true;
    }
    const unsigned index = static_cast<unsigned>((address >> 4) & 511U);
    auto& line = data_cache[index];
    if (!line.valid || line.tag != (physical & 0xfffff000U)) {
        if (!fill_data_cache(line, physical, index))
            return false;
    }
    value = 0;
    for (unsigned byte = 0; byte < width; ++byte)
        value = (value << 8) | line.data[(physical & 15U) + byte];
    return true;
}

bool Cpu::write_memory(u64 address, unsigned width, u64 value, bool check_alignment) {
    if ((width != 1 && width != 2 && width != 4 && width != 8) ||
        (check_alignment && (address & (width - 1)) != 0)) {
        address_exception(address, Access::Write);
        return false;
    }
    u32 physical = 0;
    bool cached = false;
    if (!translate(address, Access::Write, physical, cached))
        return false;
    if (physical >= 0x80000000U) {
        frozen = true;
        return false;
    }
    physical &= ~(width - 1);
    if (little_endian())
        physical ^= 8U - width;
    if (!cached) {
        system_.bus.write(physical, width, value);
        add_cycles(4);
        return !frozen;
    }
    const unsigned index = static_cast<unsigned>((address >> 4) & 511U);
    auto& line = data_cache[index];
    if (!line.valid || line.tag != (physical & 0xfffff000U)) {
        if (!fill_data_cache(line, physical, index))
            return false;
    }
    for (unsigned byte = 0; byte < width; ++byte) {
        line.data[(physical & 15U) + byte] = static_cast<u8>(value >> ((width - byte - 1) * 8));
    }
    line.dirty = true;
    return true;
}

void Cpu::cache_operation(unsigned operation, u64 address) {
    if ((address & 3U) != 0) {
        address_exception(address, Access::Read);
        return;
    }
    u32 physical = 0;
    bool cached = false;
    if (!translate(address, Access::Read, physical, cached))
        return;
    const unsigned index = static_cast<unsigned>((address >> 4) & 511U);
    auto& data = data_cache[index];
    auto& instruction = instruction_cache[(address >> 5) & 511U];
    const u32 tag = physical & 0xfffff000U;
    const bool data_hit = data.valid && data.tag == tag;
    const bool instruction_hit = instruction.valid && instruction.tag == tag;
    switch (operation) {
    case 0x00:
        instruction.tag = tag;
        instruction.valid = false;
        return;
    case 0x01:
        if (data.valid) {
            if (data.dirty && !writeback(data, index))
                return;
            data.tag = tag;
        }
        data.valid = false;
        return;
    case 0x04:
        cp0[28] = (instruction.tag >> 4) | (instruction.valid ? 0x80U : 0U);
        return;
    case 0x05:
        cp0[28] = (data.tag >> 4) | (data.valid ? 0xc0U : 0U);
        return;
    case 0x08:
        instruction.tag = static_cast<u32>((cp0[28] & 0x0fffff00U) << 4);
        instruction.valid = (cp0[28] & 0x80U) != 0;
        return;
    case 0x09:
        data.tag = static_cast<u32>((cp0[28] & 0x0fffff00U) << 4);
        data.valid = (cp0[28] & 0x80U) != 0;
        return;
    case 0x0d:
        if (!data_hit) {
            if (data.valid && data.dirty && !writeback(data, index))
                return;
            data.dirty = false;
        }
        data.tag = tag;
        data.valid = true;
        return;
    case 0x10:
        if (instruction_hit)
            instruction.valid = false;
        return;
    case 0x11:
        if (data_hit)
            data.valid = false;
        return;
    case 0x14: {
        instruction.tag = tag;
        const u32 base = tag | (static_cast<u32>(address) & 0xfe0U);
        if (!system_.bus.read_cache(base, instruction.data))
            return;
        instruction.valid = true;
        add_cycles(48);
        return;
    }
    case 0x15:
        if (data_hit) {
            if (data.dirty && !writeback(data, index))
                return;
            data.valid = false;
        }
        return;
    case 0x18:
        if (instruction_hit) {
            const u32 base = instruction.tag | (static_cast<u32>(address) & 0xfe0U);
            if (!system_.bus.write_cache(base, instruction.data))
                return;
            add_cycles(48);
        }
        return;
    case 0x19:
        if (data_hit && data.dirty) {
            if (!writeback(data, index))
                return;
            data.dirty = false;
        }
        return;
    default:
        return;
    }
}

bool Cpu::load_partial(u64 address, unsigned width, bool left, unsigned target) {
    u64 memory = 0;
    if (!read_memory(address & ~static_cast<u64>(width - 1), width, memory))
        return false;
    unsigned offset = static_cast<unsigned>(address & (width - 1));
    if (little_endian())
        offset = width - 1 - offset;
    const unsigned shift = (left ? offset : width - 1 - offset) * 8;
    const u64 width_mask = width == 8 ? ~0ULL : 0xffffffffULL;
    const u64 loaded_mask = left ? (width_mask << shift) & width_mask : width_mask >> shift;
    const u64 incoming = left ? memory << shift : memory >> shift;
    const u64 result = (gpr[target] & ~loaded_mask) | (incoming & loaded_mask);
    if (width == 4 && (left || shift == 0))
        gpr[target] = sign_extend32(static_cast<u32>(result));
    else
        gpr[target] = result;
    return true;
}

bool Cpu::store_partial(u64 address, unsigned width, bool left, u64 value) {
    const bool little = little_endian();
    const unsigned offset = static_cast<unsigned>(address & (width - 1));
    if (width == 4 && !left && !little) {
        // SWR presents its original effective address on the word and halfword transactions.
        if (offset == 0)
            return write_memory(address, 1, value, false);
        if (offset == 1)
            return write_memory(address, 2, value, false);
        if (offset == 2)
            return write_memory(address, 1, value, false) && write_memory(address - 2, 2, value >> 8, false);
        return write_memory(address, 4, value, false);
    }
    const u64 base = address & ~static_cast<u64>(width - 1);
    const bool ascending = left != little;
    unsigned position = ascending ? offset : 0;
    const unsigned end = ascending ? width : offset + 1;
    unsigned remaining = end - position;
    struct Transfer {
        u64 address;
        unsigned width;
        u64 value;
    };
    std::array<Transfer, 3> transfers{};
    unsigned transfer_count = 0;
    // Split the enabled byte lanes into aligned bus transactions without reading the device.
    while (remaining != 0) {
        unsigned chunk = width;
        while (chunk > remaining || (position & (chunk - 1)) != 0)
            chunk >>= 1;
        const unsigned consumed = ascending ? position - offset : position;
        const unsigned total = ascending ? width - offset : offset + 1;
        const unsigned shift = little
                                   ? (left ? (width - total + consumed) * 8 : consumed * 8)
                                   : (left ? (width - consumed - chunk) * 8 : (total - consumed - chunk) * 8);
        transfers[transfer_count++] = {base + position, chunk, value >> shift};
        position += chunk;
        remaining -= chunk;
    }
    for (unsigned index = 0; index < transfer_count; ++index) {
        const auto& transfer = transfers[little ? transfer_count - index - 1 : index];
        if (!write_memory(transfer.address, transfer.width, transfer.value))
            return false;
    }
    return true;
}

} // namespace cupid
