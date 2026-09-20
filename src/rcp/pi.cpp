#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

unsigned Bus::pi_domain(u32 address) {
    const u32 region = address >> 24U;
    return region == 5 || (region >= 8 && region <= 15) ? 9U : 5U;
}

u32 Bus::pi_page_mask(u32 address) const {
    return (1U << (pi_[pi_domain(address) + 2] + 2)) - 1;
}

u64 Bus::pi_dma_cycles(u32 length) const {
    const unsigned domain = pi_domain(pi_[1]);
    const u64 bytes = static_cast<u64>(length | 1U) + 1;
    const u64 page_size = 1ULL << (pi_[domain + 2] + 2);
    const u64 offset = pi_[1] & (page_size - 1);
    const u64 pages = (offset + bytes - 1) / page_size + 1;
    u64 buffers = 0;
    u64 partial_bytes = 0;
    if (pages == 1) {
        if (bytes == 128)
            buffers = 1;
        else
            partial_bytes = bytes;
    } else {
        if (offset == 0)
            ++buffers;
        else
            partial_bytes += page_size - offset;
        const u64 tail = (offset + bytes) % page_size;
        if (tail == 0)
            ++buffers;
        else
            partial_bytes += tail;
        buffers += (pages - 2) * page_size / 128;
    }
    const u64 page_cycles = (15 + pi_[domain]) * pages;
    const u64 halfword_cycles = (pi_[domain + 1] + pi_[domain + 3] + 2) * (bytes / 2);
    return page_cycles + halfword_cycles + buffers * 28 + partial_bytes;
}

u64 Bus::pi_dma_progress_deadline(u32 bytes) const {
    const auto& transfer = pi_dma_transfer_;
    const u64 offset = transfer.start_cart & (transfer.page_size - 1U);
    const u64 pages = (offset + bytes - 1U) / transfer.page_size + 1U;
    const u64 cartridge_cycles = static_cast<u64>(transfer.page_cycles) * pages +
                                 static_cast<u64>(transfer.halfword_cycles) * (bytes / 2U);
    const u64 buffer_cycles = transfer.buffer_cycles * bytes / transfer.total_bytes;
    return cartridge_cycles + buffer_cycles;
}

void Bus::start_pi_dma(u32 length) {
    pi_dma_transfer_ = {};
    auto& transfer = pi_dma_transfer_;
    transfer.start_dram = pi_[0];
    transfer.start_cart = pi_[1];
    transfer.total_bytes = (length | 1U) + 1U;
    transfer.remaining = static_cast<s32>(length + 1U);

    const unsigned domain = pi_domain(transfer.start_cart);
    transfer.page_size = 1U << (pi_[domain + 2] + 2U);
    transfer.page_cycles = 15U + pi_[domain];
    transfer.halfword_cycles = pi_[domain + 1] + pi_[domain + 3] + 2U;
    const u64 offset = transfer.start_cart & (transfer.page_size - 1U);
    const u64 pages = (offset + transfer.total_bytes - 1U) / transfer.page_size + 1U;
    const u64 cartridge_cycles = static_cast<u64>(transfer.page_cycles) * pages +
                                 static_cast<u64>(transfer.halfword_cycles) * (transfer.total_bytes / 2U);
    transfer.buffer_cycles = pi_dma_counter_ - cartridge_cycles;

    if (!pi_dma_cart_to_dram_)
        pi_[2] = transfer.total_bytes;
    select_cart(transfer.start_cart);
    transfer.cart_selected = true;
    schedule_pi_dma_progress();
}

void Bus::prepare_pi_dma_block() {
    auto& transfer = pi_dma_transfer_;
    if (!pi_dma_cart_to_dram_ || transfer.block_ready || transfer.remaining <= 0)
        return;

    transfer.block_misalign = pi_[0] & 7U;
    transfer.block_row_distance = 0x800U - (pi_[0] & 0x7ffU);
    const u32 block_limit =
        std::min(transfer.max_block_size - transfer.block_misalign, transfer.block_row_distance);
    transfer.block_length = std::min(static_cast<u32>(transfer.remaining), block_limit);
    transfer.block_bus_bytes = (transfer.block_length + 1U) & ~1U;
    transfer.block_filled = 0;
    transfer.block_ready = true;
}

void Bus::schedule_pi_dma_progress() {
    auto& transfer = pi_dma_transfer_;
    if (!pi_dma_pending_ || transfer.bus_bytes >= transfer.total_bytes) {
        pi_dma_progress_counter_ = 0;
        return;
    }

    u32 increment = 0;
    if (pi_dma_cart_to_dram_) {
        prepare_pi_dma_block();
        increment = transfer.block_bus_bytes - transfer.block_filled;
    } else {
        increment = 128U - (transfer.bus_bytes & 127U);
    }

    const u32 page_mask = transfer.page_size - 1U;
    const u32 cart_address = transfer.start_cart + transfer.bus_bytes;
    const u32 page_bytes = transfer.page_size - (cart_address & page_mask);
    increment = std::min({increment, page_bytes, transfer.total_bytes - transfer.bus_bytes});

    const u32 target = transfer.bus_bytes + increment;
    transfer.next_progress_cycle = pi_dma_progress_deadline(target);
    pi_dma_progress_counter_ = transfer.next_progress_cycle - transfer.progress_cycles;
}

void Bus::progress_pi_dma() {
    if (!pi_dma_pending_)
        return;

    auto& transfer = pi_dma_transfer_;
    const u32 target = [&] {
        u32 increment = 0;
        if (pi_dma_cart_to_dram_) {
            prepare_pi_dma_block();
            increment = transfer.block_bus_bytes - transfer.block_filled;
        } else {
            increment = 128U - (transfer.bus_bytes & 127U);
        }
        const u32 page_mask = transfer.page_size - 1U;
        const u32 cart_address = transfer.start_cart + transfer.bus_bytes;
        const u32 page_bytes = transfer.page_size - (cart_address & page_mask);
        increment = std::min({increment, page_bytes, transfer.total_bytes - transfer.bus_bytes});
        return transfer.bus_bytes + increment;
    }();

    const u32 page_mask = transfer.page_size - 1U;
    if (pi_dma_cart_to_dram_) {
        while (transfer.bus_bytes < target) {
            const u32 cart_address = transfer.start_cart + transfer.bus_bytes;
            if (!transfer.cart_selected || (transfer.bus_bytes != 0 && (cart_address & page_mask) == 0)) {
                select_cart(cart_address);
                transfer.cart_selected = true;
            }
            const u16 data = cart_read_half();
            transfer.buffer[transfer.block_filled] = static_cast<u8>(data >> 8U);
            if (transfer.block_filled + 1U < transfer.buffer.size())
                transfer.buffer[transfer.block_filled + 1U] = static_cast<u8>(data);
            transfer.block_filled += 2U;
            transfer.bus_bytes += 2U;
            transfer.remaining -= 2;
            pi_[1] = cart_address + 2U;
        }

        if (transfer.block_filled == transfer.block_bus_bytes) {
            const s32 write_length = std::max<s32>(0, static_cast<s32>(transfer.block_length) -
                                                          static_cast<s32>(transfer.block_misalign));
            if (transfer.first_block && transfer.block_length < 127U - transfer.block_misalign) {
                for (s32 index = 0; index < write_length; ++index)
                    write_ram_byte(pi_[0]++, transfer.buffer[static_cast<std::size_t>(index)]);
            } else {
                for (s32 index = 0; index < write_length; index += 2) {
                    write_ram_byte(pi_[0]++, transfer.buffer[static_cast<std::size_t>(index)]);
                    write_ram_byte(pi_[0]++, transfer.buffer[static_cast<std::size_t>(index + 1)]);
                }
            }
            pi_[0] = (pi_[0] + 7U) & ~7U;
            pi_[3] = transfer.block_length <= 8U ? 127U - transfer.block_misalign : 127U;
            transfer.first_block = false;
            transfer.max_block_size =
                transfer.block_row_distance < 8U ? 128U - transfer.block_misalign : 128U;
            transfer.block_ready = false;
        }
    } else {
        while (transfer.bus_bytes < target) {
            const u32 cart_address = transfer.start_cart + transfer.bus_bytes;
            if (!transfer.cart_selected || (transfer.bus_bytes != 0 && (cart_address & page_mask) == 0)) {
                select_cart(cart_address);
                transfer.cart_selected = true;
            }
            const u32 dram_address = transfer.start_dram + transfer.bus_bytes;
            const u16 data = static_cast<u16>((static_cast<u16>(read_ram_byte(dram_address)) << 8U) |
                                              read_ram_byte(dram_address + 1U));
            cart_write_half(data);
            transfer.bus_bytes += 2U;
        }
    }

    transfer.progress_cycles = transfer.next_progress_cycle;
    schedule_pi_dma_progress();
}

void Bus::finish_pi_dma() {
    if (!pi_dma_pending_)
        return;
    pi_dma_progress_counter_ = 0;
    pi_dma_pending_ = false;
    pi_dma_busy_ = false;
    pi_interrupt_ = true;
    set_interrupt(4, true);
}

} // namespace cupid
