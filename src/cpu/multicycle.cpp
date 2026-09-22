#include "cupid/system.hpp"

namespace cupid {

void Cpu::complete_multicycle_instruction(u64 extra_cycles) {
    if (!executing_step_ || speculative_refill_count_ == 0) {
        add_cycles(extra_cycles);
        return;
    }

    // The EX multicycle interlock and RF instruction-cache interlock progress
    // together. Start both waits after the pending fetch and issue cycles.
    synchronize();
    const u64 start = instruction_cycles_;
    complete_speculative_refills();
    const u64 refill_cycles = instruction_cycles_ - start;
    if (extra_cycles > refill_cycles)
        add_cycles(extra_cycles - refill_cycles);
}

} // namespace cupid
