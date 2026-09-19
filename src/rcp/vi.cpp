#include "cupid/system.hpp"

namespace cupid {

u32 Bus::read_vi(u32 offset) const {
    const unsigned index = static_cast<unsigned>((offset & 0x3fU) >> 2U);
    if (index == 4)
        return vi_current_;
    if (index >= vi_.size())
        return 0;
    return vi_[index];
}

void Bus::write_vi(u32 offset, u32 value) {
    const unsigned index = static_cast<unsigned>((offset & 0x3fU) >> 2U);
    if (index >= vi_.size())
        return;
    if (index == 4) {
        set_interrupt(3, false);
        return;
    }
    static constexpr std::array<u32, 14> masks = {
        0x0000ffffU, 0x00ffffffU, 0x00000fffU, 0x000003ffU, 0,           0x3fffffffU, 0x000003ffU,
        0x001f0fffU, 0x0fff0fffU, 0x03ff03ffU, 0x03ff03ffU, 0x03ff03ffU, 0x0fff0fffU, 0x0fff0fffU,
    };
    vi_[index] = value & masks[index];
}

void Bus::tick_vi(u64 rcp_cycles) {
    constexpr u64 rcp_frequency = 62500000;
    const u64 fraction = (rcp_cycles % rcp_frequency) * system_.video_frequency() + vi_clock_fraction_;
    const u64 clocks = (rcp_cycles / rcp_frequency) * system_.video_frequency() + fraction / rcp_frequency;
    vi_clock_fraction_ = fraction % rcp_frequency;
    if ((vi_[0] & 3U) == 0) {
        vi_counter_ = 0;
        vi_current_ &= 1U;
        return;
    }
    vi_counter_ += clocks;
    while (true) {
        u64 line_cycles = (vi_[7] & 0xfffU) + 1U;
        if ((vi_current_ >> 1) == 1) {
            const unsigned selection = (vi_[7] >> (16 + vi_leap_counter_)) & 1U;
            line_cycles = (vi_[8] >> (selection * 16)) & 0xfffU;
        }
        if (vi_counter_ < line_cycles)
            break;
        vi_counter_ -= line_cycles;

        const bool interlaced = (vi_[6] & 1U) == 0;
        vi_current_ += 2;
        if (vi_current_ >= vi_[6] + 1) {
            vi_current_ = (vi_current_ & 1U) ^ static_cast<u32>(interlaced);
            vi_leap_counter_ = (vi_leap_counter_ + 1) % 5;
        }

        const u32 compare = vi_[3];
        bool interrupt = false;
        if (!interlaced || (compare & 1U) != 0) {
            interrupt = (vi_current_ >> 1) == (compare >> 1);
        } else if ((vi_current_ & 1U) != 0) {
            interrupt = vi_current_ + 1 == compare;
        } else {
            interrupt = vi_current_ == compare || (vi_current_ == vi_[6] && compare == 0);
        }
        if (interrupt)
            set_interrupt(3, true);
    }
}

} // namespace cupid
