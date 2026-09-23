#include "cupid/rsp.hpp"

#include <algorithm>

namespace cupid {

u64 Rsp::local_cycle_budget(u64 maximum_cycles) {
    if (!local_execution_ready())
        return 0;
    if (dma_busy_) {
        if (dma_cycles_until_row_ <= 1)
            return 0;
        maximum_cycles = std::min(maximum_cycles, dma_cycles_until_row_ - 1);
    }
    const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
    const bool trusted = memory.imem_trusted();
    const u64 revision = memory.imem_revision();
    u32 address = pc;
    bool pairing = !pipeline_.single_issue_;
    auto& first = pipeline_.size() != 0U ? pipeline_.current_fetch()
                  : trusted
                      ? pipeline_.prepare(imem, revision, pairing, pc)
                      : pipeline_.prepare(fetch_instruction(pc), fetch_instruction(pc + 4), pairing, pc);
    if (!first.issued_local || maximum_cycles == 0)
        return 0;
    if (branch_pending_)
        return 1;
    const unsigned first_index =
        pipeline_.size() != 0U ? pipeline_.current_index_ : RspPipeline::cache_index(pc, pairing);
    // A stalled packet may retain old words across an IMEM write. Its own
    // local cycle is safe, but its old following packets must be revalidated.
    if (!trusted || pipeline_.decoded_revision_[first_index] != revision)
        return 1;
    if (first.local_cycles != 0U)
        return std::min<u64>(maximum_cycles, first.local_cycles);
    u64 budget = 0;
    while (budget < 64) {
        const auto& packet = budget == 0 && pipeline_.size() != 0U ? pipeline_.current_fetch()
                             : trusted ? pipeline_.prepare(imem, revision, pairing, address)
                                       : pipeline_.prepare(fetch_instruction(address),
                                                           fetch_instruction(address + 4), pairing, address);
        if (!packet.issued_local)
            break;
        ++budget;
        // Each packet takes at least one cycle. Dependencies and a pending
        // branch bubble can only postpone its effects. Stop before following
        // an unresolved branch, including an already pending delay slot.
        if ((packet.flags & RspPipeline::branch) != 0U)
            break;
        address = (address + packet.count * 4U) & 0x0ffcU;
        if (address == pc)
            break;
        pairing = true;
    }
    first.local_cycles = static_cast<u8>(budget);
    return std::min(maximum_cycles, budget);
}

u64 Rsp::run_local(u64 maximum_cycles) {
    if (!local_execution_ready())
        return 0;

    if (dma_busy_) {
        // Transfer the row before its issue cycle in tick().
        if (dma_cycles_until_row_ <= 1)
            return 0;
        maximum_cycles = std::min(maximum_cycles, dma_cycles_until_row_ - 1);
    }

    if (!memory.imem_trusted()) {
        RspPipeline::LocalWindow window;
        const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
        u64 elapsed = 0;
        while (elapsed < maximum_cycles) {
            const auto issue =
                pipeline_.size() == 0U ? pipeline_.local_issue(imem, window, pc) : pipeline_.local_issue();
            if (issue == RspPipeline::LocalIssue::Blocked)
                break;
            if (issue == RspPipeline::LocalIssue::Ready)
                execute_group();
            ++elapsed;
        }
        tick_dma(elapsed);
        return elapsed;
    }

    const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
    const u64 revision = memory.imem_revision();
    u64 elapsed = 0;
    while (elapsed < maximum_cycles) {
        if (pipeline_.size() == 0U) {
            const bool pairing = !pipeline_.single_issue_;
            auto& decoded = pipeline_.prepare(imem, revision, pairing, pc);
            if (!decoded.issued_local)
                break;
            // A branch bubble must not latch the next fetch. DMA or a host
            // write can replace that instruction at the following boundary.
            if (pipeline_.advance_branch_wait()) {
                ++elapsed;
                continue;
            }
            pipeline_.current_index_ = RspPipeline::cache_index(pc, pairing);
            pipeline_.count_ = decoded.count;
        } else if (!pipeline_.current_fetch().issued_local) {
            break;
        }
        const auto remaining = static_cast<unsigned>(std::min<u64>(3, maximum_cycles - elapsed));
        elapsed += pipeline_.advance_operand_wait(remaining);
        if (elapsed == maximum_cycles)
            break;
        execute_group();
        ++elapsed;
    }
    tick_dma(elapsed);
    return elapsed;
}

} // namespace cupid
