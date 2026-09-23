#include "cupid/system.hpp"

#include <algorithm>
#include <cassert>

namespace cupid {

void System::advance_cached_rsp_ticks(u64 rcp_cycles) {
    // The CPU slice stops before every peripheral edge and has no buffered
    // stores. Its register/cache operations cannot observe SP or DP state.
    // Shared RSP operations still see each clock and DMA transfer before issue.
    assert(!settling_ && deferred_rcp_ == 0 && peripheral_debt_ == 0);
    struct SettlementScope {
        bool& active;
        ~SettlementScope() {
            active = false;
        }
    } scope{settling_};
    settling_ = true;
    while (rcp_cycles != 0) {
        if (rsp_lead_enabled_ && coupled_event_gap_ > 1)
            rsp.run_ahead(std::min(rsp_lead_cycles, coupled_event_gap_ - 1));
        // Local cycles executed ahead need only their clocks; they cannot raise
        // an interrupt or observe the RDP clock.
        u64 elapsed = rsp.consume_lead(rcp_cycles);
        if (elapsed != 0) {
            bus.tick_clocks(elapsed);
        } else {
            bus.tick_clocks(1);
            rsp.tick(1);
            elapsed = 1;
        }
        rcp_cycles -= elapsed;
        coupled_event_gap_ -= std::min(coupled_event_gap_, elapsed);
    }
}

} // namespace cupid
