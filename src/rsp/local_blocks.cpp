#include "cupid/rsp.hpp"

namespace cupid {

void Rsp::prepare_local_block(LocalBlock& block, u64 revision) {
    const auto incoming = pipeline_.snapshot();
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
        if (!decoded.issued_local || (decoded.flags & RspPipeline::branch) != 0U ||
            block.count + decoded.count > block.instructions.size())
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
    }
    block.next_pc = address;
    block.outgoing = pipeline_.snapshot();
    pipeline_.restore(incoming);
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

    for (unsigned index = 0; index < block.count; ++index) {
        const auto& instruction = block.instructions[index];
        execute_decoded(instruction.word, instruction.operation, instruction.operands);
    }
    gpr_[0] = 0;
    // These instructions cannot observe the PC or enter a shared register handler.
    current_pc_ = (block.next_pc - 4U) & 0x0ffcU;
    pc = pc_shadow_ = block.next_pc;
    next_pc_ = (pc + 4U) & 0x0ffcU;
    pipeline_.restore(block.outgoing);
    return block.cycles;
}

} // namespace cupid
