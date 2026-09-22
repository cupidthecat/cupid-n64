#include "cupid/system.hpp"

#include <algorithm>

namespace cupid {

u64 System::idle_loop_event_cycles() const {
    if (rsp.running() || cpu.next_buffered_write() != 0 || !bus.pending_outputs_.empty())
        return 0;
    // VI can interrupt without a presentation callback or enabled RDRAM refresh.
    const u64 rcp_cycles = std::min({bus.next_event(), bus.next_vi_line(), rsp.next_dma_event()});
    return cpu_cycles_for_rcp(rcp_cycles);
}

} // namespace cupid
