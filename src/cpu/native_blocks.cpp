#include "cupid/cpu.hpp"

#include <cassert>

namespace cupid {

unsigned Cpu::execute_cached_native(CachedLinePlan& plan, unsigned slot, unsigned maximum_steps,
                                    u64 maximum_cycles,
                                    std::optional<std::array<u64, 32>>* rollback_registers,
                                    CpuNativeState& state) {
    if (!native_execution_ || slot >= CpuNativeCode::maximum_instructions || maximum_steps < 2U ||
        next_pc != pc + 4U || in_delay_slot_ || pending_load_register_ != 0 || pending_fpu_register_ != 32)
        return 0;
    auto& block = plan.native[slot];
    const unsigned minimum = block.ends_branch ? 2U : 3U;
    if (block.count < minimum || block.count > maximum_steps ||
        static_cast<u64>(block.count) + block.extra_cycles > maximum_cycles)
        return 0;
    if (!block.code && block.visits < 5U) {
        ++block.visits;
        if (block.visits == 1U || block.visits == 5U) {
            std::array<u32, CpuNativeCode::maximum_instructions> instructions{};
            for (unsigned index = 0; index < block.count; ++index)
                instructions[index] = read_be32(plan.image.data() + (slot + index) * 4U);
            const auto words = std::span(instructions).first(block.count);
            // A replaced hardware line can reuse code without repeating compilation warmup.
            // A miss still waits for five eligible visits before creating a program.
            block.code = block.visits == 1U ? native_cache_.find(words) : native_cache_.lookup(words);
        }
    }
    if (!block.code)
        return 0;

    std::optional<std::array<u64, 32>> local_rollback;
    auto* rollback = rollback_registers != nullptr       ? rollback_registers
                     : block.has_load || block.has_store ? &local_rollback
                                                         : nullptr;
    if (rollback != nullptr)
        rollback->emplace(gpr);
    state.branch_link = pc + static_cast<u64>(block.count) * 4U + 4U;
    if (!block.code->execute(state)) {
        assert((block.has_load || block.has_store) && rollback != nullptr && rollback->has_value());
        gpr = **rollback;
        return 0;
    }
    pc += static_cast<u64>(block.count) * 4U;
    next_pc = pc + 4U;
    if (block.ends_branch) {
        const u32 instruction = read_be32(plan.image.data() + (slot + block.count - 1U) * 4U);
        const unsigned opcode = instruction >> 26U;
        if (opcode == 0U)
            next_pc = state.branch_target;
        else if (opcode == 2U || opcode == 3U)
            next_pc = (pc & ~0x0fffffffULL) | state.branch_target;
        else if (state.branch_taken != 0U)
            next_pc = pc + (sign_extend16(static_cast<u16>(instruction)) << 2U);
        in_delay_slot_ = true;
    }
    following_pc_ = next_pc;
    following_delay_slot_ = block.ends_branch;
    annul_next_ = false;
    pending_load_register_ = block.pending_load;
    instruction_cycles_ = block.last_cycles;
    fetched_instruction_ = {pc, read_be32(plan.image.data() + (slot + block.count) * 4U), true};
    native_block_instructions_ += block.count;
    return block.count;
}

} // namespace cupid
