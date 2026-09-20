#include "cupid/bus.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace cupid {

u64 Bus::next_event() const {
    u64 next = std::numeric_limits<u64>::max();
    const auto include = [&](bool pending, u64 cycles) {
        if (pending)
            next = std::min(next, std::max<u64>(1, cycles));
    };
    include(pi_io_busy_, pi_io_counter_);
    include(pi_dma_pending_, pi_dma_counter_);
    include(flash_busy_counter_ != 0, flash_busy_counter_);
    include(si_io_busy_, si_io_counter_);
    include(si_dma_pending_, si_dma_counter_);
    include(eeprom_busy_counter_ != 0, eeprom_busy_counter_);
    if (rtc)
        include(rtc->running(), rtc->next_tick());
    for (const auto& pak : transfer_paks)
        if (const auto* cartridge = pak.cartridge())
            include(cartridge->clock_running(), cartridge->next_tick());
    include(ri_refresh_counter_ != 0, ri_refresh_counter_);
    if (((ri_[4] & 0x20000U) != 0 && memory.bus_active()) || (video_output_ && (vi_[0] & 3U) != 0))
        include(true, next_vi_line());
    if (video_output_ || (ai_fifo_count_ != 0 && (ai_lengths_[0] == 0 || (ai_[2] & 1U) != 0))) {
        const u64 remaining = ai_clock_period_ > ai_counter_ ? ai_clock_period_ - ai_counter_ : 0;
        include(true, (remaining + ai_clock_rate_ - 1) / ai_clock_rate_);
    }
    return next;
}

void Bus::tick(u64 rcp_cycles) {
    while (rcp_cycles != 0) {
        const u64 elapsed = std::min(rcp_cycles, next_event());
        tick_devices(elapsed);
        dispatch_outputs();
        rcp_cycles -= elapsed;
    }
}

void Bus::dispatch_outputs() {
    struct DeliveryScope {
        bool& active;
        ~DeliveryScope() {
            active = false;
        }
    } scope{output_delivery_active_};
    output_delivery_active_ = true;
    while (!pending_outputs_.empty()) {
        auto output = std::move(pending_outputs_.front());
        pending_outputs_.pop_front();
        output();
    }
    ai_boundary_pending_ = false;
}

} // namespace cupid
