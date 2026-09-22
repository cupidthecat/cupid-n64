#include "cupid/system.hpp"

#include "cupid/rcp/clocks.hpp"

namespace cupid::cpu_timing {

u64 cache_miss_sclock_extra(u64 pending_cpu_cycles, u64 system_fraction) {
    // The cache-miss sequence spends two PClock cycles before synchronizing to SClock.
    const u64 pending_phase = (pending_cpu_cycles % 3) * 2;
    const u64 phase_at_sync = (system_fraction % 3 + pending_phase + 1) % 3;
    return rcp::cpu_cycles_for_rcp(1, phase_at_sync) - 1;
}

} // namespace cupid::cpu_timing

namespace cupid {

u64 Cpu::cache_miss_sclock_extra() const {
    if (!executing_step_)
        return 0;
    const u64 pending = instruction_cycles_ - synchronized_instruction_cycles_;
    return cpu_timing::cache_miss_sclock_extra(pending, system_.rcp_fraction_);
}

} // namespace cupid
