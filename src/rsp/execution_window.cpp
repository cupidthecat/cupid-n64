#include "cupid/rsp.hpp"

#include <algorithm>

namespace cupid {

namespace {

bool ends_linear_prefix(u32 instruction) {
    const unsigned op = instruction >> 26U;
    return (op >= 1U && op <= 7U) || (op == 0U && ((instruction & 63U) == 8U || (instruction & 63U) == 9U));
}

} // namespace

unsigned Rsp::local_execution_window() {
    if (!local_execution_ready() || dma_busy_ || dma_full_ || branch_pending_ ||
        next_pc_ != ((pc + 4U) & 0x0ffcU))
        return 0;

    unsigned latched_cycles = 0;
    u32 address = pc;
    if (pipeline_.size() != 0U) {
        const auto& fetched = pipeline_.decoded_[pipeline_.current_index_];
        if (!fetched.issued_local)
            return 0;
        if ((fetched.flags & RspPipeline::branch) != 0U)
            return 1;
        // The already fetched words remain authoritative during an operand wait.
        latched_cycles = 1;
        address = (address + pipeline_.size() * 4U) & 0x0ffcU;
    }

    const unsigned line_index = (address >> 6U) & 63U;
    const auto* bytes = memory.data() + 0x1000U + line_index * 64U;
    auto& line = local_code_lines_[line_index];
    if (!line.valid || !std::equal(line.image.begin(), line.image.end(), bytes)) {
        std::copy_n(bytes, line.image.size(), line.image.begin());
        unsigned prefix = 0;
        for (unsigned index = 16U; index != 0U; --index) {
            const u32 word = read_be32(line.image.data() + (index - 1U) * 4U);
            if (!RspPipeline::instruction_is_local(word))
                prefix = 0;
            else if (ends_linear_prefix(word))
                prefix = 1;
            else
                ++prefix;
            line.prefix[index - 1U] = static_cast<u8>(prefix);
        }
        line.valid = true;
    }

    // At most two words issue per cycle. Waits can only postpone reaching the
    // first branch or shared instruction. The caller consumes this bound before
    // any DMA, callback, or CPU operation that can change the RSP's state.
    return latched_cycles + line.prefix[(address >> 2U) & 15U] / 2U;
}

} // namespace cupid
