#include "cupid/bus.hpp"

#include <algorithm>
#include <limits>

namespace cupid {

u64 Bus::next_event() const {
    u64 next = std::numeric_limits<u64>::max();
    const auto include = [&](bool pending, u64 cycles) {
        if (pending)
            next = std::min(next, std::max<u64>(1, cycles));
    };
    include(pi_io_busy_, pi_io_counter_);
    include(pi_dma_pending_, pi_dma_counter_);
    include(si_io_busy_, si_io_counter_);
    include(si_dma_pending_, si_dma_counter_);
    include(eeprom_busy_counter_ != 0, eeprom_busy_counter_);
    if (ai_fifo_count_ != 0 && (ai_lengths_[0] == 0 || (ai_[2] & 1U) != 0)) {
        const u64 remaining = ai_clock_period_ > ai_counter_ ? ai_clock_period_ - ai_counter_ : 0;
        include(true, (remaining + ai_clock_rate_ - 1) / ai_clock_rate_);
    }
    return next;
}

void Bus::tick(u64 rcp_cycles) {
    while (rcp_cycles != 0) {
        const u64 elapsed = std::min(rcp_cycles, next_event());
        tick_devices(elapsed);
        rcp_cycles -= elapsed;
    }
}

} // namespace cupid
