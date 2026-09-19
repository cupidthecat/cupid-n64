#include "cupid/system.hpp"

#include <algorithm>

namespace cupid {
namespace {

u64 read_dma_cycles(std::span<const u8, 64> packet, const std::array<ControllerState, 4>& controllers) {
    u64 cycles = 13600;
    std::size_t offset = 0;
    unsigned channel = 0;
    while (offset < packet.size() && channel < 5) {
        const u8 send = packet[offset++];
        if (send == 0xfe) {
            cycles += 1420;
            break;
        }
        if (send == 0 || send == 0xfd || send == 0xff) {
            cycles += 1420;
            if (send != 0xff)
                ++channel;
            continue;
        }
        if (offset == packet.size())
            break;
        const u8 receive = packet[offset++];
        offset += (send & 0x3fU) + (receive & 0x3fU);
        cycles += channel == 4 ? 20000U : controllers[channel].connected ? 22000U : 18000U;
        ++channel;
    }
    return cycles;
}

} // namespace

u64 Bus::read_pif(u32 physical, unsigned width) {
    if (width == 8)
        return 0;
    u32 word = 0;
    if (si_io_busy_) {
        word = si_bus_latch_;
        si_io_busy_ = false;
        si_io_counter_ = 0;
    } else {
        word = read_pif_word(physical);
    }
    open_bus_ = word;
    return extract_word_lane(word, physical, width);
}

void Bus::write_pif(u32 physical, unsigned width, u64 value) {
    if (si_io_busy_)
        return;
    si_io_busy_ = true;
    si_dma_busy_ = true;
    si_phase_ = 0x9b0;
    si_bus_latch_ = expand_rcp_write(physical, width, value);
    si_io_counter_ = 2150;
    write_pif_word(physical, si_bus_latch_);
    if ((physical & 0x7ffU) >= 0x7c0U)
        process_pif_control();
}

u32 Bus::read_si(u32 offset) const {
    const unsigned index = static_cast<unsigned>((offset & 0x1fU) >> 2U);
    if (index == 6) {
        return (si_dma_busy_ ? 1U : 0U) | (si_io_busy_ ? 2U : 0U) | si_phase_ |
               (si_interrupt_ ? (1U << 12U) : 0U);
    }
    return index < si_.size() ? si_[index] : 0;
}

void Bus::write_si(u32 offset, u32 value) {
    const unsigned index = static_cast<unsigned>((offset & 0x1fU) >> 2U);
    switch (index) {
    case 0:
        si_[0] = value & 0x00fffff8U;
        return;
    case 1:
        si_[1] = value & ~1U;
        si_dma_pif_to_dram_ = true;
        si_dma_pending_ = true;
        si_dma_busy_ = true;
        si_phase_ = 0x140;
        si_dma_counter_ = read_dma_cycles(std::span<const u8, 64>(pif.data() + 0x7c0, 64), controllers);
        return;
    case 4:
        si_[4] = value & ~1U;
        si_dma_pif_to_dram_ = false;
        si_dma_pending_ = true;
        si_dma_busy_ = true;
        si_phase_ = 0x410;
        si_dma_counter_ = 4065;
        return;
    case 6:
        si_interrupt_ = false;
        set_interrupt(1, false);
        return;
    default:
        return;
    }
}

void Bus::finish_si_dma() {
    if (!si_dma_pending_)
        return;
    const u32 dram = si_[0] & 0x00fffff8U;
    if (si_dma_pif_to_dram_) {
        process_pif();
        for (u32 index = 0; index < 64; index += 4)
            memory.write(dram + index, 4, read_pif_word(si_[1] + index));
    } else {
        for (u32 index = 0; index < 64; index += 4)
            write_pif_word(si_[4] + index, static_cast<u32>(memory.read(dram + index, 4)));
        process_pif_control();
    }
    si_dma_pending_ = false;
    si_dma_busy_ = false;
    si_phase_ = 0;
    si_interrupt_ = true;
    set_interrupt(1, true);
}

void Bus::tick_si(u64 rcp_cycles) {
    while (rcp_cycles != 0 && (si_dma_pending_ || si_io_busy_)) {
        u64 elapsed = rcp_cycles;
        if (si_dma_pending_)
            elapsed = std::min(elapsed, si_dma_counter_);
        if (si_io_busy_)
            elapsed = std::min(elapsed, si_io_counter_);
        rcp_cycles -= elapsed;
        if (si_dma_pending_)
            si_dma_counter_ -= elapsed;
        if (si_io_busy_)
            si_io_counter_ -= elapsed;
        if (si_io_busy_ && si_io_counter_ == 0) {
            si_io_busy_ = false;
            si_dma_busy_ = false;
            si_phase_ = 0;
            si_interrupt_ = true;
            set_interrupt(1, true);
        }
        if (si_dma_pending_ && si_dma_counter_ == 0)
            finish_si_dma();
    }
}

} // namespace cupid
