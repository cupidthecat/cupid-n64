#include "cupid/rsp.hpp"

namespace cupid {

void Rsp::prepare_local_block(LocalBlock& block, u64 revision) {
    const auto incoming = pipeline_.snapshot();
    const auto previous_instructions = block.instructions;
    const unsigned previous_count = block.count;
    const unsigned previous_terminal_count = block.terminal_count;
    const bool previous_attempted = block.native_attempted;
    auto previous_native = std::move(block.native);
    block = {};
    block.incoming = incoming.previous;
    block.single_issue = incoming.single_issue;
    block.revision = revision;
    block.valid = true;
    u32 address = pc;
    const std::span<const u8, 4096> imem(memory.internal_data() + 0x1000U, 4096U);
    while (block.count < block.instructions.size()) {
        const bool pairing = !pipeline_.single_issue_;
        const auto& decoded = pipeline_.prepare(imem, revision, pairing, address);
        if (!decoded.issued_local || block.count + decoded.count > block.instructions.size())
            break;
        bool control = false;
        for (unsigned index = 0; index < decoded.count; ++index)
            control = control || decoded.operations[index] == RspPipeline::Operation::Cop0;
        if (control)
            break;

        pipeline_.current_index_ = RspPipeline::cache_index(address, pairing);
        pipeline_.count_ = decoded.count;
        block.cycles += pipeline_.advance_operand_wait(3) + 1U;
        for (unsigned index = 0; index < decoded.count; ++index) {
            const u32 word = decoded.words[index];
            block.instructions[block.count++] = {word, decoded.operations[index],
                                                 decode_scalar_operands(word)};
        }
        address = (address + decoded.count * 4U) & 0x0ffcU;
        pipeline_.retire(false, address);
        if ((decoded.flags & RspPipeline::branch) != 0U) {
            block.terminal_count = decoded.count;
            break;
        }
    }
    block.next_pc = address;
    block.outgoing = pipeline_.snapshot();
    pipeline_.restore(incoming);
    bool same_instructions = block.count == previous_count && block.terminal_count == previous_terminal_count;
    for (unsigned index = 0; same_instructions && index < block.count; ++index)
        same_instructions = block.instructions[index].word == previous_instructions[index].word;
    if (same_instructions) {
        block.native = std::move(previous_native);
        block.native_attempted = previous_attempted;
    }
}

u64 Rsp::execute_local_block(u64 revision, u64 maximum_cycles) {
    if (!local_blocks_)
        local_blocks_.reset(std::make_unique<std::array<LocalBlock, 1024>>());
    auto& block = (*local_blocks_)[pc >> 2U];
    if (!block.valid || block.revision != revision || block.incoming != pipeline_.previous_ ||
        block.single_issue != pipeline_.single_issue_)
        prepare_local_block(block, revision);
    // A partial block must preserve the ordinary fetch and stall boundary.
    if (block.count < 4U || block.cycles > maximum_cycles)
        return 0;
    const unsigned local_count = block.count - block.terminal_count;

    if (native_execution_ && RspNativeCode::available() && !block.native_attempted) {
        std::array<RspNativeInstruction, 16> instructions{};
        for (unsigned index = 0; index < local_count; ++index)
            instructions[index] = {block.instructions[index].word, block.instructions[index].operation};
        auto lookup = native_cache_.lookup(std::span(instructions).first(local_count));
        block.native = std::move(lookup.code);
        block.native_attempted = lookup.complete;
    }
    if (native_execution_ && block.native) {
        RspNativeState state{this, gpr_.data(), memory.internal_data()};
        block.native->execute(state);
        native_block_instructions_ += local_count;
    } else {
        for (unsigned index = 0; index < local_count; ++index) {
            const auto& instruction = block.instructions[index];
            execute_decoded(instruction.word, instruction.operation, instruction.operands);
        }
    }
    gpr_[0] = 0;
    // The prefix cannot observe the PC. A final branch packet uses its actual
    // instruction addresses and leaves the delay slot to the next issue group.
    pc = pc_shadow_ = (block.next_pc - block.terminal_count * 4U) & 0x0ffcU;
    next_pc_ = (pc + 4U) & 0x0ffcU;
    current_pc_ = (pc - 4U) & 0x0ffcU;
    for (unsigned index = local_count; index < block.count; ++index) {
        const auto& instruction = block.instructions[index];
        current_pc_ = pc;
        pc = next_pc_;
        next_pc_ = (pc + 4U) & 0x0ffcU;
        execute_decoded(instruction.word, instruction.operation, instruction.operands);
        gpr_[0] = 0;
        pc_shadow_ = pc;
    }
    pipeline_.restore(block.outgoing);
    return block.cycles;
}

void Rsp::set_native_execution(bool enabled) {
    native_execution_ = enabled;
}

} // namespace cupid
