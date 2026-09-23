#include "cupid/system.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <span>

namespace cupid {

u64 Rsp::consume_lead(u64 rcp_cycles) {
    const u64 consumed = std::min(rcp_cycles, lead_cycles_);
    lead_cycles_ -= consumed;
    lead_elapsed_ += consumed;
    return consumed;
}

bool Rsp::can_issue_local() {
    if (!local_execution_ready() || (dma_busy_ && dma_cycles_until_row_ <= 1))
        return false;
    if (pipeline_.size() != 0U)
        return pipeline_.current_fetch().issued_local;
    const bool pairing_allowed = !pipeline_.single_issue_;
    if (memory.imem_trusted()) {
        const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
        return pipeline_.prepare(imem, memory.imem_revision(), pairing_allowed, pc).issued_local;
    }
    return pipeline_.prepare(fetch_instruction(pc), fetch_instruction(pc + 4), pairing_allowed, pc)
        .issued_local;
}

void Rsp::save_dmem_block(u32 block) {
    u64& blocks = saved_dmem_blocks_[block >> 6U];
    const u64 bit = 1ULL << (block & 63U);
    if ((blocks & bit) != 0)
        return;
    blocks |= bit;
    std::copy_n(memory.internal_data() + block * 16U, 16U, saved_dmem_[block].begin());
}

void Rsp::run_ahead(u64 maximum_cycles) {
    // Extending a partly consumed lead would lengthen the replay without bound.
    if (lead_cycles_ != 0 || maximum_cycles == 0 || !can_issue_local())
        return;

    auto& state = checkpoint_;
    state.gpr = gpr_;
    state.vr = vr_;
    state.accumulator = accumulator_;
    state.vcol = vcol_;
    state.vcoh = vcoh_;
    state.vccl = vccl_;
    state.vcch = vcch_;
    state.vce = vce_;
    state.div_input = div_input_;
    state.div_output = div_output_;
    state.div_input_high = div_input_high_;
    state.dma_cycles_until_row = dma_cycles_until_row_;
    state.pc = pc;
    state.next_pc = next_pc_;
    state.pc_shadow = pc_shadow_;
    state.current_pc = current_pc_;
    state.branch_pending = branch_pending_;
    state.dma_pending = dma_pending_;
    state.pipeline = pipeline_.snapshot();
    saved_dmem_blocks_.fill(0);
    lead_elapsed_ = 0;

    tracking_dmem_ = true;
    while (lead_cycles_ < maximum_cycles) {
        const u64 elapsed = execute_local(maximum_cycles - lead_cycles_);
        if (elapsed == 0)
            break;
        lead_cycles_ += elapsed;
    }
    tracking_dmem_ = false;
}

void Rsp::rewind_lead() {
    if (lead_cycles_ == 0)
        return;

    const auto& state = checkpoint_;
    gpr_ = state.gpr;
    vr_ = state.vr;
    accumulator_ = state.accumulator;
    vcol_ = state.vcol;
    vcoh_ = state.vcoh;
    vccl_ = state.vccl;
    vcch_ = state.vcch;
    vce_ = state.vce;
    div_input_ = state.div_input;
    div_output_ = state.div_output;
    div_input_high_ = state.div_input_high;
    dma_cycles_until_row_ = state.dma_cycles_until_row;
    pc = state.pc;
    next_pc_ = state.next_pc;
    pc_shadow_ = state.pc_shadow;
    current_pc_ = state.current_pc;
    branch_pending_ = state.branch_pending;
    dma_pending_ = state.dma_pending;
    pipeline_.restore(state.pipeline);
    for (u32 word = 0; word < saved_dmem_blocks_.size(); ++word) {
        for (u64 blocks = saved_dmem_blocks_[word]; blocks != 0; blocks &= blocks - 1) {
            const u32 block = word * 64U + static_cast<u32>(std::countr_zero(blocks));
            std::copy_n(saved_dmem_[block].begin(), 16U, memory.internal_data() + block * 16U);
        }
    }

    // Nothing outside the RSP changed its inputs since the checkpoint, so the
    // replay reaches the shared clock through the same instruction sequence.
    u64 remaining = lead_elapsed_;
    lead_cycles_ = 0;
    lead_elapsed_ = 0;
    while (remaining != 0) {
        const u64 elapsed = execute_local(remaining);
        assert(elapsed != 0);
        if (elapsed == 0)
            break;
        remaining -= elapsed;
    }
}

bool Rsp::local_execution_ready() const {
    return !halted_ && !single_step_ && pc == pc_shadow_;
}

bool Rsp::step_local() {
    RspPipeline::LocalIssue issue = RspPipeline::LocalIssue::Blocked;
    if (pipeline_.size() == 0U) {
        if (memory.imem_trusted()) {
            const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
            issue = pipeline_.local_issue(imem, memory.imem_revision(), pc);
        } else {
            const std::array<u32, 2> words{fetch_instruction(pc), fetch_instruction(pc + 4)};
            issue = pipeline_.local_issue(words[0], words[1], pc);
        }
    } else {
        issue = pipeline_.local_issue();
    }

    if (issue == RspPipeline::LocalIssue::Blocked)
        return false;
    if (issue == RspPipeline::LocalIssue::Ready)
        execute_group();
    return true;
}

u64 Rsp::run_local(u64 maximum_cycles) {
    if (lead_cycles_ != 0)
        return consume_lead(maximum_cycles);
    return execute_local(maximum_cycles);
}

u64 Rsp::execute_local(u64 maximum_cycles) {
    if (!local_execution_ready())
        return 0;

    if (dma_busy_) {
        // The row's payload stays unchanged before its transfer cycle. Leave that
        // cycle to tick(), which transfers the row before issuing its instruction.
        if (dma_cycles_until_row_ <= 1)
            return 0;
        maximum_cycles = std::min(maximum_cycles, dma_cycles_until_row_ - 1);
    }

    if (maximum_cycles < 8) {
        u64 elapsed = 0;
        while (elapsed < maximum_cycles && step_local())
            ++elapsed;
        tick_dma(elapsed);
        return elapsed;
    }

    RspPipeline::LocalWindow window;
    const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
    const bool trusted_imem = memory.imem_trusted();
    const u64 imem_revision = memory.imem_revision();
    const auto step_window = [&] {
        RspPipeline::LocalIssue issue = RspPipeline::LocalIssue::Blocked;
        if (pipeline_.size() == 0U) {
            issue = trusted_imem ? pipeline_.local_issue(imem, imem_revision, pc)
                                 : pipeline_.local_issue(imem, window, pc);
        } else {
            issue = pipeline_.local_issue();
        }

        if (issue == RspPipeline::LocalIssue::Blocked)
            return false;
        if (issue == RspPipeline::LocalIssue::Ready)
            execute_group();
        return true;
    };

    u64 elapsed = 0;
    while (elapsed < maximum_cycles && step_window()) {
        // A branch-wait or operand-wait cycle is still an RSP cycle. Local groups
        // cannot change shared registers, start DMA, halt, or raise an SP interrupt.
        ++elapsed;
    }
    tick_dma(elapsed);
    return elapsed;
}

} // namespace cupid
