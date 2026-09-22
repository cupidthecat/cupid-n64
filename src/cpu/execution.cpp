#include "cupid/system.hpp"

#include <algorithm>
#include <limits>

namespace cupid {
namespace {

u32 advance_random(u32 current, u32 wired, unsigned instructions) {
    const u32 before_reset = (current - wired) & 63U;
    if (instructions <= before_reset)
        return (current - instructions) & 63U;
    const u32 period = ((31U - wired) & 63U) + 1;
    return (31U - (instructions - before_reset - 1) % period) & 63U;
}

} // namespace

bool Cpu::branches_to_self(u32 instruction, u64 address) {
    if ((instruction & 0xfc00ffffU) == 0x1000ffffU &&
        ((instruction >> 21) & 31U) == ((instruction >> 16) & 31U))
        return true;
    return (instruction >> 26) == 2 &&
           (((address + 4) & ~0x0fffffffULL) | ((instruction & 0x03ffffffULL) << 2)) == address;
}

void Cpu::advance_batched_instruction_counters(unsigned instructions) {
    random_ = advance_random(random_, random_wired_, instructions);
    instruction_count += instructions;
}

unsigned Cpu::run_slice(unsigned maximum_steps, u64 maximum_cycles) {
    const u64 start = cycles;
    unsigned steps = 0;
    while (steps < maximum_steps && cycles - start < maximum_cycles && !frozen) {
        const unsigned batched = batch_idle_loop(maximum_steps - steps, maximum_cycles - (cycles - start));
        if (batched != 0) {
            steps += batched;
        } else {
            const unsigned cached =
                batch_cached_private(maximum_steps - steps, maximum_cycles - (cycles - start));
            if (cached != 0) {
                steps += cached;
            } else {
                step();
                ++steps;
            }
        }
    }
    system_.settle();
    return steps;
}

unsigned Cpu::batch_idle_loop(unsigned maximum_steps, u64 maximum_cycles) {
    if (maximum_steps < 2 || maximum_cycles < 2 || !fetched_instruction_.valid ||
        fetched_instruction_.address != pc || !branches_to_self(fetched_instruction_.instruction, pc))
        return 0;

    // Only a cached branch and its NOP delay slot can repeat without observing memory.
    // Other segments and partially retired pipeline state use ordinary instruction steps.
    if ((pc & 0xffffffffe0000003ULL) != 0xffffffff80000000ULL ||
        (next_pc & 0xffffffffe0000003ULL) != 0xffffffff80000000ULL || !kernel_mode() || little_endian() ||
        next_pc != pc + 4 || following_pc_ != pc + 4 || in_delay_slot_ || following_delay_slot_ ||
        annul_next_ || redirected_ || exception_pending || executing_step_ || nmi_pending_ || gpr[0] != 0 ||
        pending_load_register_ != 0 || pending_fpu_register_ != 32 || count_write_hold_ != 0 ||
        software_interrupt_delay_ != 0 || instruction_cycles_ != 1 || synchronized_instruction_cycles_ != 1 ||
        speculative_refill_count_ != 0 || wired_writes_[0].instruction != 0 ||
        wired_writes_[1].instruction != 0)
        return 0;

    update_interrupt_inputs();
    if ((status() & 7U) == 1U && (static_cast<u32>(cp0[13]) & status() & 0xff00U) != 0)
        return 0;

    const auto cached_word = [&](u64 address, u32 expected) {
        const auto& line = instruction_cache[(address >> 5) & 511U];
        const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
        return line.valid && line.tag == (physical & 0xfffff000U) &&
               read_be32(line.data.data() + (physical & 31U)) == expected;
    };
    if (!cached_word(pc, fetched_instruction_.instruction) || !cached_word(next_pc, 0))
        return 0;

    const u64 device_edge = system_.idle_loop_event_cycles();
    if (device_edge <= 2)
        return 0;
    const u32 distance = static_cast<u32>(cp0[11]) - static_cast<u32>(cp0[9]);
    const u64 count_ticks = distance == 0 ? (1ULL << 32) : distance;
    const u64 timer_edge = count_ticks * 2 - static_cast<u64>(count_half_);
    // Leave every event-producing instruction to step(), including odd-cycle boundaries.
    u64 amount = std::min(static_cast<u64>(maximum_steps), maximum_cycles);
    amount = std::min(amount, device_edge - 1);
    amount = std::min(amount, timer_edge - 1);
    amount = std::min(amount, std::numeric_limits<u64>::max() - instruction_count);
    amount = std::min(amount, std::numeric_limits<u64>::max() - cycles);
    if (system_.rsp.running()) {
        const auto steps = static_cast<unsigned>(system_.run_local_rsp_for_idle(amount));
        if (steps == 0)
            return 0;

        advance_batched_instruction_counters(steps);
        batched_idle_instructions_ += steps;
        if ((steps & 1U) != 0) {
            // One unmatched branch instruction leaves the cached NOP delay slot fetched.
            next_pc = following_pc_ = pc;
            pc += 4;
            in_delay_slot_ = following_delay_slot_ = true;
            fetched_instruction_ = {pc, 0, true};
        }
        advance_clock_counters(steps);
        system_.advance_after_local_rsp(steps);
        update_interrupt_inputs();
        return steps;
    }
    const auto steps = static_cast<unsigned>(amount & ~1ULL);
    if (steps == 0)
        return 0;

    advance_batched_instruction_counters(steps);
    batched_idle_instructions_ += steps;
    update_clocks(steps);
    return steps;
}

} // namespace cupid
