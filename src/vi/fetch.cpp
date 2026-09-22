#include "cupid/system.hpp"

#include <algorithm>

namespace cupid {

u64 Bus::vi_line_rcp_cycles() const {
    constexpr u64 rcp_frequency = 62500000;
    const u64 period = vi_line_period_.value_or(vi_line_cycles());
    return std::max<u64>(1, (period * rcp_frequency + system_.video_frequency() - 1) /
                                system_.video_frequency());
}

u32 Bus::vi_line_bytes() const {
    const unsigned format = vi_[0] & 3U;
    if (format < 2)
        return 0;
    return (vi_[2] & 0xfffU) * (format == 3 ? 4U : 2U);
}

// Address of the framebuffer word the VI line-buffer fill is reading now. The
// fill runs on every line while a framebuffer type is selected; outside the
// vertical active window it stays on the first or last visible source line.
std::optional<u32> Bus::vi_fetch_address() const {
    const u32 line_bytes = vi_line_bytes();
    if (line_bytes == 0 || !memory.bus_active())
        return std::nullopt;
    const s32 first_line = static_cast<s32>((vi_[10] >> 16) >> 1);
    const s32 rows = (static_cast<s32>(vi_[10] & 1023U) - static_cast<s32>(vi_[10] >> 16)) / 2;
    const s32 line = static_cast<s32>(vi_current_ >> 1);
    const s32 row = std::clamp(line - first_line, 0, std::max(rows - 1, 0));
    const u32 sample_y = (vi_[13] >> 16) + static_cast<u32>(row) * (vi_[13] & 4095U);
    const u64 period = vi_line_period_.value_or(vi_line_cycles());
    const u64 progress = period == 0 ? 0 : std::min(vi_counter_, period - 1) * line_bytes / period;
    const u32 address =
        (vi_[1] & 0xffffffU) + (sample_y >> 10) * line_bytes + static_cast<u32>(progress & ~7ULL);
    return address & 0x00ffffffU;
}

// The fill consumes one eight-byte RDRAM word at a steady rate across the line.
u64 Bus::vi_fetch_interval() const {
    const u32 line_bytes = vi_line_bytes();
    if (line_bytes == 0)
        return 1;
    return std::max<u64>(1, vi_line_rcp_cycles() * 8 / line_bytes);
}

} // namespace cupid
