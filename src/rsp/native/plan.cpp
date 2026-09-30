#include "plan.hpp"

#include "vector.hpp"

#if defined(CUPID_RSP_NATIVE)
namespace cupid::rsp_native {

VectorPlan plan_vectors(std::span<const RspNativeInstruction> instructions) {
    using Op = RspPipeline::Operation;
    VectorPlan plan;
    unsigned consecutive_vectors = 0, accumulations = 0;
    for (const auto instruction : instructions) {
        if (instruction.operation == Op::Cop2 && (instruction.word & (1U << 25U)) != 0U &&
            supports_vector(instruction.word & 63U)) {
            ++consecutive_vectors;
            const unsigned function = instruction.word & 63U;
            accumulations += function >= 8U && function <= 15U;
            plan.cache_accumulator =
                plan.cache_accumulator || (consecutive_vectors >= 3U && accumulations >= 2U);
        } else if (instruction.operation == Op::Cop2 || instruction.operation == Op::VectorLoad ||
                   instruction.operation == Op::VectorStore || instruction.operation == Op::Lh ||
                   instruction.operation == Op::Lhu || instruction.operation == Op::Lw ||
                   instruction.operation == Op::Sb || instruction.operation == Op::Sh ||
                   instruction.operation == Op::Sw) {
            consecutive_vectors = accumulations = 0;
        }
    }

    // Every register is observable at block exit. Helpers form barriers because
    // their vector reads and writes are outside this emitted-operation analysis.
    std::array<bool, 32> live;
    live.fill(true);
    for (std::size_t index = instructions.size(); index != 0; --index) {
        const auto instruction = instructions[index - 1U];
        if (instruction.operation == Op::VectorLoad || instruction.operation == Op::VectorStore) {
            live.fill(true);
        } else if (instruction.operation == Op::Cop2) {
            const u32 word = instruction.word;
            const unsigned function = word & 63U;
            if ((word & (1U << 25U)) == 0U || !supports_vector(function)) {
                live.fill(true);
                continue;
            }
            const unsigned destination = (word >> 6U) & 31U;
            plan.destination_live[index - 1U] = live[destination];
            live[destination] = false;
            // VSAR reads only the accumulator, ignoring encoded VS and VT.
            if (function != 29U) {
                live[(word >> 11U) & 31U] = true;
                live[(word >> 16U) & 31U] = true;
            }
        }
    }
    return plan;
}

} // namespace cupid::rsp_native
#endif
