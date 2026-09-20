#include "cupid/bus.hpp"
#include "cupid/system.hpp"

#include <algorithm>
#include <limits>

namespace cupid {

void Bus::reset_ai_clock() {
    ai_counter_ = 0;
    ai_clock_rate_ = static_cast<u64>(system_.video_frequency()) * 44100;
    ai_clock_period_ = 62500000ULL * system_.video_frequency();
    ai_clock_started_ = false;
    ai_dac_rate_written_ = false;
}

void Bus::latch_ai_period() {
    if (ai_dac_rate_written_)
        ai_clock_period_ = 62500000ULL * 44100 * (ai_[4] + 1U);
}

u32 Bus::read_ai(u32 offset) const {
    const unsigned index = (offset & 0x1fU) >> 2U;
    if (index != 3)
        return ai_lengths_[0] & 0x3ffffU;
    u32 value = (1U << 20U) | (1U << 24U);
    if ((ai_[2] & 1U) != 0)
        value |= 1U << 25U;
    if (ai_fifo_count_ != 0)
        value |= 1U << 30U;
    if (ai_fifo_count_ > 1)
        value |= (1U << 31U) | 1U;
    return value;
}

void Bus::write_ai(u32 offset, u32 value) {
    switch ((offset & 0x1fU) >> 2U) {
    case 0:
        if (ai_fifo_count_ < 2)
            ai_addresses_[ai_fifo_count_] = value & 0x00fffff8U;
        return;
    case 1:
        if (ai_fifo_count_ < 2) {
            ai_lengths_[ai_fifo_count_] = value & 0x0003fff8U;
            if (ai_fifo_count_ == 0)
                set_interrupt(2, true);
            ++ai_fifo_count_;
        }
        return;
    case 2:
        ai_[2] = value & 1U;
        return;
    case 3:
        set_interrupt(2, false);
        return;
    case 4:
        ai_[4] = value & 0x3fffU;
        ai_dac_rate_written_ = true;
        if (!ai_clock_started_)
            latch_ai_period();
        return;
    case 5:
        ai_[5] = value & 0xfU;
        return;
    default:
        return;
    }
}

void Bus::sample_ai() {
    u32 sample = 0;
    bool consumed = false;
    if (ai_fifo_count_ != 0 && ai_lengths_[0] != 0 && (ai_[2] & 1U) != 0) {
        // The carry from bit 12 is applied to the next sample, including across a FIFO handoff.
        if (ai_address_carry_)
            ai_addresses_[0] = (ai_addresses_[0] + 0x2000U) & 0x00ffffffU;
        const u32 address = ai_addresses_[0];
        for (u32 byte = 0; byte < 4; ++byte)
            sample = (sample << 8U) | read_ram_byte(address + byte);
        const u32 low = (address + 4U) & 0x1fffU;
        ai_addresses_[0] = (address & 0x00ffe000U) | low;
        ai_address_carry_ = low == 0;
        ai_lengths_[0] -= 4;
        consumed = true;
    }
    if (ai_fifo_count_ != 0 && ai_lengths_[0] == 0) {
        if (--ai_fifo_count_ != 0) {
            ai_addresses_[0] = ai_addresses_[1];
            ai_lengths_[0] = ai_lengths_[1];
            set_interrupt(2, true);
        }
    }
    if (consumed && audio_output)
        audio_output(static_cast<s16>(sample >> 16U), static_cast<s16>(sample));
}

void Bus::tick_ai(u64 rcp_cycles) {
    // Keep the oscillator fraction even while DMA is disabled or the FIFO is empty.
    while (rcp_cycles != 0) {
        const u64 chunk =
            std::min(rcp_cycles, (std::numeric_limits<u64>::max() - ai_counter_) / ai_clock_rate_);
        ai_counter_ += chunk * ai_clock_rate_;
        rcp_cycles -= chunk;
        while (ai_counter_ >= ai_clock_period_) {
            ai_counter_ -= ai_clock_period_;
            sample_ai();
            latch_ai_period();
            if (ai_fifo_count_ == 0 || ((ai_[2] & 1U) == 0 && ai_lengths_[0] != 0)) {
                ai_counter_ %= ai_clock_period_;
                break;
            }
        }
    }
}

} // namespace cupid
