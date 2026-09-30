#include "cupid/cpu.hpp"

#include <cassert>

namespace cupid {

unsigned Cpu::execute_cached_native(CachedLinePlan& plan, unsigned slot, unsigned maximum_steps,
                                    u64 maximum_cycles,
                                    std::optional<std::array<u64, 32>>* rollback_registers,
                                    CpuNativeState& state) {
    if (!native_execution_ || slot >= CpuNativeCode::maximum_instructions || maximum_steps < 3U ||
        next_pc != pc + 4U || in_delay_slot_ || pending_load_register_ != 0 || pending_fpu_register_ != 32)
        return 0;
    auto& block = plan.native[slot];
    if (block.count < 3U || block.count > maximum_steps ||
        static_cast<u64>(block.count) + block.extra_cycles > maximum_cycles)
        return 0;
    if (!block.code && block.visits < 5U && ++block.visits == 5U) {
        std::array<u32, CpuNativeCode::maximum_instructions> instructions{};
        for (unsigned index = 0; index < block.count; ++index)
            instructions[index] = read_be32(plan.image.data() + (slot + index) * 4U);
        block.code = native_cache_.lookup(std::span(instructions).first(block.count));
    }
    if (!block.code)
        return 0;

    std::optional<std::array<u64, 32>> local_rollback;
    auto* rollback = rollback_registers != nullptr       ? rollback_registers
                     : block.has_load || block.has_store ? &local_rollback
                                                         : nullptr;
    if (rollback != nullptr)
        rollback->emplace(gpr);
    if (!block.code->execute(state)) {
        assert((block.has_load || block.has_store) && rollback != nullptr && rollback->has_value());
        gpr = **rollback;
        return 0;
    }
    pc += static_cast<u64>(block.count) * 4U;
    next_pc = following_pc_ = pc + 4U;
    following_delay_slot_ = annul_next_ = false;
    pending_load_register_ = block.pending_load;
    instruction_cycles_ = block.last_cycles;
    fetched_instruction_ = {pc, read_be32(plan.image.data() + (slot + block.count) * 4U), true};
    native_block_instructions_ += block.count;
    return block.count;
}

} // namespace cupid
