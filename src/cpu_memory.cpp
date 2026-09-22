#include "cupid/system.hpp"

#include <algorithm>
#include <cassert>

namespace cupid {
namespace {

// Measured difference between an uncached word load that hits the open RDRAM
// row and one that must open a different row in the same bank.
constexpr u64 rdram_row_open_cycles = 4;

u64 preview_rcp_cycles(u64 cpu_cycles, u64 rcp_fraction) {
    const u64 whole = cpu_cycles / 3;
    const u64 fraction = (cpu_cycles % 3) * 2 + rcp_fraction;
    return whole * 2 + fraction / 3;
}

u64 appended_rcp_wait_cpu_cycles(u64 rcp_cycles, u64 preceding_cpu_cycles, u64 rcp_fraction) {
    if (rcp_cycles == 0)
        return 0;
    const u64 end_fraction = ((preceding_cpu_cycles % 3) * 2 + rcp_fraction) % 3;
    return (rcp_cycles * 3 - end_fraction + 1) / 2;
}

} // namespace

u64 Cpu::rdram_refresh_delay(u32 physical) const {
    if (!executing_step_ || physical >= 0x03f00000U)
        return 0;
    const u64 remaining = system_.bus.rdram_refresh_wait();
    return remaining == 0 ? 0 : system_.cpu_cycles_for_rcp(remaining);
}

void Cpu::complete_speculative_refills() {
    if (speculative_refill_count_ == 0)
        return;

    // Evaluate queued requests from the CPU/system phase reached by the caller.
    synchronize();
    for (unsigned index = 0; index < speculative_refill_count_; ++index) {
        const u32 base = speculative_refill_bases_[index];
        constexpr u64 nominal = 48;
        const u64 miss_delay = nominal + cache_miss_sclock_extra();
        const u64 refresh_overlap =
            system_.bus.rdram_refresh_overlap(base, preview_rcp_cycles(miss_delay, system_.rcp_fraction_));
        const u64 overlap = appended_rcp_wait_cpu_cycles(refresh_overlap, miss_delay, system_.rcp_fraction_);
        add_cycles(miss_delay + rdram_refresh_delay(base) + overlap);
        synchronize();
    }
    speculative_refill_count_ = 0;
}

void Cpu::address_exception(u64 address, Access access) {
    if (speculative_fetch_)
        return;
    cp0[8] = address;
    cp0[10] = (cp0[10] & 255U) | (address & 0xc00000ffffffe000ULL);
    cp0[4] = (cp0[4] & ~0x7fffffULL) | ((address >> 9) & 0x7ffff0U);
    cp0[20] =
        (cp0[20] & ~0x1ffffffffULL) | ((address >> 9) & 0x7ffffff0U) | ((address >> 31) & 0x180000000ULL);
    raise_exception(access == Access::Write ? Exception::AddressStore : Exception::AddressLoad, 0, false,
                    access == Access::Execute);
}

void Cpu::tlb_exception(u64 address, Access access, bool refill, bool modification) {
    if (speculative_fetch_)
        return;
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
    drain_write_buffer();
    const u32 base = line.tag | ((index << 4) & 0xff0U);
    constexpr u64 nominal = 40;
    const u64 refresh_overlap = system_.bus.rdram_refresh_overlap(
        base, executing_step_ ? preview_rcp_cycles(nominal, system_.rcp_fraction_) : 0);
    const u64 overlap = appended_rcp_wait_cpu_cycles(refresh_overlap, nominal, system_.rcp_fraction_);
    add_cycles(nominal + rdram_refresh_delay(base) + overlap);
    synchronize();
    return system_.bus.write_cache(base, line.data);
}

bool Cpu::fill_data_cache(CacheLine<16>& line, u32 physical, unsigned index) {
    if (line.valid && line.dirty && !writeback(line, index))
        return false;
    line.tag = physical & 0xfffff000U;
    const u32 base = line.tag | ((index << 4) & 0xff0U);
    drain_write_buffer();
    constexpr u64 nominal = 40;
    const u64 miss_delay = nominal + cache_miss_sclock_extra();
    const u64 refresh_overlap = system_.bus.rdram_refresh_overlap(
        base, executing_step_ ? preview_rcp_cycles(miss_delay, system_.rcp_fraction_) : 0);
    const u64 overlap = appended_rcp_wait_cpu_cycles(refresh_overlap, miss_delay, system_.rcp_fraction_);
    add_cycles(miss_delay + rdram_refresh_delay(base) + overlap);
    synchronize();
    if (!system_.bus.read_cache(base, line.data))
        return false;
    line.valid = true;
    line.dirty = false;
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
        drain_write_buffer();
        // RDRAM must return a memory response before the load can retire. Device
        // registers use a separate path and do not incur this nominal RAM delay.
        // A request whose row is not open in its bank first waits for RI to open it.
        const bool ram = physical < 0x03f00000U;
        const u64 row_wait =
            ram && executing_step_ && system_.bus.rdram_row_miss(physical) ? rdram_row_open_cycles : 0;
        const u64 nominal = (ram ? 31 : 4) + row_wait;
        const u64 refresh_overlap = system_.bus.rdram_refresh_overlap(
            physical, executing_step_ ? preview_rcp_cycles(nominal, system_.rcp_fraction_) : 0);
        const u64 overlap = appended_rcp_wait_cpu_cycles(refresh_overlap, nominal, system_.rcp_fraction_);
        add_cycles(nominal + rdram_refresh_delay(physical) + overlap);
        synchronize();
        value = system_.bus.read(physical, width);
        return !frozen;
    }
    if (instruction) {
        const unsigned index = static_cast<unsigned>((address >> 5) & 511U);
        auto& line = instruction_cache[index];
        if (!line.valid || line.tag != (physical & 0xfffff000U)) {
            line.valid = false;
            line.tag = physical & 0xfffff000U;
            const u32 base = line.tag | ((index << 5) & 0xfe0U);
            drain_write_buffer();
            constexpr u64 nominal = 48;
            if (!speculative_fetch_) {
                const u64 miss_delay = nominal + cache_miss_sclock_extra();
                const u64 refresh_overlap = system_.bus.rdram_refresh_overlap(
                    base, executing_step_ ? preview_rcp_cycles(miss_delay, system_.rcp_fraction_) : 0);
                const u64 overlap =
                    appended_rcp_wait_cpu_cycles(refresh_overlap, miss_delay, system_.rcp_fraction_);
                add_cycles(miss_delay + rdram_refresh_delay(base) + overlap);
                synchronize();
            }
            if (!system_.bus.read_cache(base, line.data))
                return false;
            line.valid = true;
            if (speculative_fetch_) {
                assert(speculative_refill_count_ < speculative_refill_bases_.size());
                speculative_refill_bases_[speculative_refill_count_++] = base;
            }
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

bool Cpu::prepare_write(u64 address, unsigned width, bool check_alignment, u32& physical, bool& cached) {
    if ((width != 1 && width != 2 && width != 4 && width != 8) ||
        (check_alignment && (address & (width - 1)) != 0)) {
        address_exception(address, Access::Write);
        return false;
    }
    if (!translate(address, Access::Write, physical, cached))
        return false;
    if (physical >= 0x80000000U) {
        frozen = true;
        return false;
    }
    physical &= ~(width - 1);
    if (little_endian())
        physical ^= 8U - width;
    return true;
}

bool Cpu::write_memory(u64 address, unsigned width, u64 value, bool check_alignment) {
    u32 physical = 0;
    bool cached = false;
    if (!prepare_write(address, width, check_alignment, physical, cached))
        return false;
    if (!cached) {
        if (executing_step_) {
            buffer_write(physical, width, value);
            return !frozen;
        }
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
        add_cycles(5);
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
        drain_write_buffer();
        constexpr u64 nominal = 48;
        const u64 miss_delay = nominal + cache_miss_sclock_extra();
        const u64 refresh_overlap = system_.bus.rdram_refresh_overlap(
            base, executing_step_ ? preview_rcp_cycles(miss_delay, system_.rcp_fraction_) : 0);
        const u64 overlap = appended_rcp_wait_cpu_cycles(refresh_overlap, miss_delay, system_.rcp_fraction_);
        add_cycles(miss_delay + rdram_refresh_delay(base) + overlap);
        synchronize();
        if (!system_.bus.read_cache(base, instruction.data))
            return;
        instruction.valid = true;
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
            drain_write_buffer();
            constexpr u64 nominal = 48;
            const u64 refresh_overlap = system_.bus.rdram_refresh_overlap(
                base, executing_step_ ? preview_rcp_cycles(nominal, system_.rcp_fraction_) : 0);
            const u64 overlap = appended_rcp_wait_cpu_cycles(refresh_overlap, nominal, system_.rcp_fraction_);
            add_cycles(nominal + rdram_refresh_delay(base) + overlap);
            synchronize();
            if (!system_.bus.write_cache(base, instruction.data))
                return;
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
        std::array<MemoryWrite, 2> transfers{};
        unsigned count = 1;
        if (offset == 0)
            transfers[0] = {address, 1, value};
        else if (offset == 1)
            transfers[0] = {address, 2, value};
        else if (offset == 2) {
            transfers[0] = {address, 1, value};
            transfers[1] = {address - 2, 2, value >> 8};
            count = 2;
        } else
            transfers[0] = {address, 4, value};
        return write_partial(std::span{transfers.data(), count});
    }
    const u64 base = address & ~static_cast<u64>(width - 1);
    const bool ascending = left != little;
    unsigned position = ascending ? offset : 0;
    const unsigned end = ascending ? width : offset + 1;
    unsigned remaining = end - position;
    std::array<MemoryWrite, 3> transfers{};
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
    if (little)
        std::reverse(transfers.begin(), transfers.begin() + transfer_count);
    return write_partial(std::span{transfers.data(), transfer_count});
}

bool Cpu::write_partial(std::span<const MemoryWrite> transfers) {
    std::array<MemoryWrite, 3> physical_transfers{};
    bool cached = false;
    for (unsigned index = 0; index < transfers.size(); ++index) {
        const auto& transfer = transfers[index];
        u32 physical = 0;
        if (!prepare_write(transfer.address, transfer.width, false, physical, cached))
            return false;
        physical_transfers[index] = {physical, transfer.width, transfer.value};
    }
    if (executing_step_ && !cached) {
        buffer_writes(std::span{physical_transfers.data(), transfers.size()});
        return !frozen;
    }
    for (const auto& transfer : transfers) {
        if (!write_memory(transfer.address, transfer.width, transfer.value, false))
            return false;
    }
    return true;
}

} // namespace cupid
