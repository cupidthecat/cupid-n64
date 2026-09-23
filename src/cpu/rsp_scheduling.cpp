#include "cupid/system.hpp"

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
        bus.tick_clocks(1);
        rsp.tick(1);
        --rcp_cycles;
    }
}

} // namespace cupid
