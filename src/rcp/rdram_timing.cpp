#include "cupid/bus.hpp"

namespace cupid {

u64 Bus::rdram_refresh_overlap(u32 physical, u64 transfer_rcp_cycles) const {
    if (physical >= 0x03f00000U || transfer_rcp_cycles == 0 || ri_refresh_counter_ != 0 ||
        (ri_[4] & 0x20000U) == 0 || !memory.bus_active() || next_vi_line() > transfer_rcp_cycles)
        return 0;

    const u32 banks = memory.bank_status();
    const bool dirty = (banks & (banks >> 8U) & 0xffU) != 0;
    return (ri_[4] >> (dirty ? 8U : 0U)) & 0xffU;
}

} // namespace cupid
