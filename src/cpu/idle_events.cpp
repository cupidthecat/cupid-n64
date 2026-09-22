#include "cupid/system.hpp"

#include <algorithm>

namespace cupid {

u64 System::idle_loop_event_cycles() {
    settle();
    if (cpu.next_buffered_write() != 0 || !bus.pending_outputs_.empty())
        return 0;
    // VI can interrupt without a presentation callback or enabled RDRAM refresh.
    const u64 rcp_cycles = std::min(std::min(bus.next_event(), bus.next_vi_line()), rsp.next_dma_event());
    return cpu_cycles_for_rcp(rcp_cycles);
}

u64 System::cached_private_event_cycles() {
    settle();
    if (rsp.dma_busy_ || rsp.dma_full_ || cpu.next_buffered_write() != 0 || !bus.pending_outputs_.empty())
        return 0;
    // The cached CPU slice keeps every materialized clock strictly before Bus/VI
    // edges. A running RSP is handled separately one proven-local tick at a time.
    const u64 rcp_cycles = std::min(bus.next_event(), bus.next_vi_line());
    return cpu_cycles_for_rcp(rcp_cycles);
}

bool System::rsp_local_execution_ready() const {
    return rsp.local_execution_ready();
}

bool System::run_local_rsp_tick() {
    return rsp.run_local(1) == 1;
}

u64 System::run_local_rsp_for_idle(u64 maximum_cpu_cycles) {
    const u64 whole = maximum_cpu_cycles / 3;
    const u64 fraction = (maximum_cpu_cycles % 3) * 2 + rcp_fraction_;
    const u64 maximum_rcp_cycles = whole * 2 + fraction / 3;
    const u64 elapsed = rsp.run_local(maximum_rcp_cycles);
    // Return the first CPU boundary that contains exactly the RSP cycles already run.
    return elapsed == 0 ? 0 : cpu_cycles_for_rcp(elapsed);
}

} // namespace cupid
