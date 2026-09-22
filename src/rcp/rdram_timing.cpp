#include "cupid/bus.hpp"

namespace cupid {

u64 Bus::rdram_refresh_overlap(u32 physical, u64 transfer_rcp_cycles) const {
    settle_system();
    if (physical >= 0x03f00000U || transfer_rcp_cycles == 0 || ri_refresh_counter_ != 0 ||
        (ri_[4] & 0x20000U) == 0 || !memory.bus_active() || next_vi_line() > transfer_rcp_cycles)
        return 0;

    const u32 banks = memory.bank_status();
    const bool dirty = (banks & (banks >> 8U) & 0xffU) != 0;
    return (ri_[4] >> (dirty ? 8U : 0U)) & 0xffU;
}

bool Bus::rdram_row_miss(u32 physical) const {
    if (physical >= 0x00800000U || !memory.bus_active())
        return false;
    settle_system();
    settle_vi_fetch(physical);
    return !memory.row_open(physical);
}

// VI fetches are not scheduled as bus events. Apply the fetches that fell
// between the bank's previous access and the current request, so the bank
// holds VI's row when the requester arrives.
void Bus::settle_vi_fetch(u32 physical) const {
    const auto fetch = vi_fetch_address();
    if (!fetch || (*fetch >> 20) != (physical >> 20))
        return;
    const u64 interval = vi_fetch_interval();
    const u64 previous = memory.bank_access_clock(physical);
    if (memory.clock() / interval == previous / interval)
        return;
    memory.open_row(*fetch);
}

} // namespace cupid
