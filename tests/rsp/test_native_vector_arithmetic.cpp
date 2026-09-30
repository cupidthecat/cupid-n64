#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <bit>
#include <memory>
#include <span>
#include <vector>

using namespace cupid;

namespace {

using Lanes = std::array<u16, 8>;
constexpr u64 accumulator_mask = (u64{1} << 48U) - 1U;

constexpr u32 vector_word(unsigned function, unsigned vd, unsigned vs, unsigned vt, unsigned element) {
    return 0x4a000000U | (element << 21U) | (vt << 16U) | (vs << 11U) | (vd << 6U) | function;
}

constexpr u32 vector_memory(unsigned opcode, unsigned vector, unsigned address, unsigned base) {
    return (opcode << 26U) | (2U << 21U) | (vector << 16U) | (4U << 11U) | ((address - base) / 16U);
}

void run(Rsp& rsp, std::span<const u32> words) {
    for (unsigned index = 0; index < words.size(); ++index)
        write_be32(rsp.memory.data() + 0x1000U + index * 4U, words[index]);
    rsp.write_pc(0);
    rsp.write_register(0x10U, 1);
    rsp.tick(1024);
    CHECK_EQ(rsp.read_register(0x10U) & 3U, 3U);
}

unsigned selected_lane(unsigned element, unsigned lane) {
    if (element < 2U)
        return lane;
    if (element < 4U)
        return (lane & ~1U) | (element & 1U);
    if (element < 8U)
        return (lane & 4U) | (element & 3U);
    return element - 8U;
}

s64 signed_accumulator(u64 bits) {
    bits &= accumulator_mask;
    if ((bits & (u64{1} << 47U)) != 0)
        bits |= ~accumulator_mask;
    return std::bit_cast<s64>(bits);
}

u16 saturated_middle(u64 bits) {
    const s64 value = signed_accumulator(bits) >> 16U;
    if (value > 32767)
        return 0x7fffU;
    if (value < -32768)
        return 0x8000U;
    return static_cast<u16>(value);
}

struct Oracle {
    std::array<Lanes, 32> vectors{};
    std::array<u64, 8> accumulator{};

    void execute(u32 word) {
        const unsigned function = word & 63U;
        const unsigned element = (word >> 21U) & 15U;
        const auto left = vectors[(word >> 11U) & 31U];
        const auto right = vectors[(word >> 16U) & 31U];
        auto& result = vectors[(word >> 6U) & 31U];
        for (unsigned lane = 0; lane < 8U; ++lane) {
            const u16 a = left[lane], b = right[selected_lane(element, lane)];
            if (function == 0x1dU) {
                result[lane] = element >= 8U && element <= 10U
                                   ? static_cast<u16>(accumulator[lane] >> ((10U - element) * 16U))
                                   : 0;
            } else if (function >= 0x28U && function <= 0x2dU) {
                u16 value = function < 0x2aU   ? static_cast<u16>(a & b)
                            : function < 0x2cU ? static_cast<u16>(a | b)
                                               : static_cast<u16>(a ^ b);
                if ((function & 1U) != 0U)
                    value = static_cast<u16>(~value);
                result[lane] = value;
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | value;
            } else {
                const s64 product =
                    (function == 4U || function == 6U ? static_cast<s64>(a)
                                                      : static_cast<s64>(std::bit_cast<s16>(a))) *
                    (function == 4U || function == 5U ? static_cast<s64>(b)
                                                      : static_cast<s64>(std::bit_cast<s16>(b)));
                const u64 shifted = static_cast<u64>(product) << 16U;
                const u64 bits = function == 0U                     ? static_cast<u64>(product * 2 + 0x8000)
                                 : function == 4U                   ? static_cast<u64>(product) >> 16U
                                 : function == 5U || function == 6U ? static_cast<u64>(product)
                                 : function == 7U                   ? shifted
                                                                    : accumulator[lane] + shifted;
                accumulator[lane] = bits & accumulator_mask;
                result[lane] = function == 4U || function == 6U ? static_cast<u16>(bits)
                               : function == 5U                 ? static_cast<u16>(bits >> 16U)
                                                                : saturated_middle(bits);
            }
        }
    }
};

void seed(Rsp& rsp, Oracle& oracle, unsigned pattern) {
    constexpr Lanes boundaries{0, 1, 0x7fffU, 0x8000U, 0xffffU, 0x4000U, 0xc000U, 0xfffeU};
    std::vector<u32> words{0x24020200U};
    for (unsigned reg = 0; reg < 32U; ++reg) {
        for (unsigned lane = 0; lane < 8U; ++lane) {
            const u16 value = pattern == 0U   ? boundaries[(lane + reg) & 7U]
                              : pattern == 1U ? u16{0x8000U}
                                              : static_cast<u16>((reg * 0x7654U + lane * 0x3211U) ^ 0xa5a5U);
            oracle.vectors[reg][lane] = value;
            write_be16(rsp.memory.data() + 0x200U + reg * 16U + lane * 2U, value);
        }
        words.push_back(vector_memory(0x32U, reg, 0x200U + reg * 16U, 0x200U));
    }
    const u32 initial = vector_word(5U, 30U, 28U, 29U, 0);
    words.push_back(initial);
    oracle.execute(initial);
    for (unsigned flag = 0; flag < 3U; ++flag) {
        constexpr std::array<u16, 3> values{0x5aa5U, 0xa55aU, 0x96U};
        words.push_back(0x34010000U | values[flag]);
        words.push_back(0x48c10000U | (flag << 11U));
    }
    words.push_back(13);
    run(rsp, words);
}

void verify(Rsp& rsp, const Oracle& oracle) {
    std::vector<u32> words{0x24020400U};
    for (unsigned reg = 0; reg < 32U; ++reg)
        words.push_back(vector_memory(0x3aU, reg, 0x400U + reg * 16U, 0x400U));
    for (unsigned slice = 0; slice < 3U; ++slice) {
        words.push_back(vector_word(0x1dU, 31U, 0, 0, 8U + slice));
        words.push_back(vector_memory(0x3aU, 31U, 0x600U + slice * 16U, 0x400U));
        words.push_back(0x48410000U | (slice << 11U));
        words.push_back(0xac010630U + slice * 4U);
    }
    words.push_back(13);
    run(rsp, words);
    for (unsigned reg = 0; reg < 32U; ++reg)
        for (unsigned lane = 0; lane < 8U; ++lane)
            CHECK_EQ(read_be16(rsp.memory.data() + 0x400U + reg * 16U + lane * 2U),
                     oracle.vectors[reg][lane]);
    for (unsigned slice = 0; slice < 3U; ++slice)
        for (unsigned lane = 0; lane < 8U; ++lane)
            CHECK_EQ(read_be16(rsp.memory.data() + 0x600U + slice * 16U + lane * 2U),
                     static_cast<u16>(oracle.accumulator[lane] >> ((2U - slice) * 16U)));
    CHECK_EQ(read_be32(rsp.memory.data() + 0x630U), 0x5aa5U);
    CHECK_EQ(read_be32(rsp.memory.data() + 0x634U), 0xffffa55aU);
    CHECK_EQ(read_be32(rsp.memory.data() + 0x638U), 0x96U);
}

} // namespace

TEST(rsp_native_vector_arithmetic_matches_scalar_oracle_for_all_registers_elements_and_aliases) {
    for (unsigned function : {4U, 5U, 6U, 7U, 15U, 29U, 40U, 41U, 42U, 43U, 44U, 45U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            for (unsigned alias = 0; alias < 4U; ++alias) {
                const unsigned source = (function + element) & 31U;
                const unsigned target = alias == 3U ? source : (source + 17U) & 31U;
                const unsigned destination = alias == 0U   ? (source + 9U) & 31U
                                             : alias == 2U ? target
                                                           : source;
                const u32 word = vector_word(function, destination, source, target, element);
                const std::array instructions{RspNativeInstruction{word, RspPipeline::Operation::Cop2}};
                const auto code = RspNativeCode::compile(instructions);
                CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
                if (!code)
                    continue;
                for (unsigned pattern = 0; pattern < 3U; ++pattern) {
                    auto machine = std::make_unique<System>();
                    machine->rsp.set_native_execution(false);
                    Oracle oracle;
                    seed(machine->rsp, oracle, pattern);
                    RspNativeState state{&machine->rsp, nullptr, nullptr};
                    // Repeated signed products cross both 32-bit endpoints and
                    // the 48-bit sign boundary; logical writes retain upper slices.
                    for (unsigned repeat = 0; repeat < 5U; ++repeat) {
                        code->execute(state);
                        oracle.execute(word);
                    }
                    verify(machine->rsp, oracle);
                }
            }
        }
    }
}

TEST(rsp_native_vector_arithmetic_keeps_accumulator_dependencies_across_helper_calls) {
    for (unsigned element = 0; element < 16U; ++element) {
        std::array<RspNativeInstruction, 9> instructions{};
        constexpr std::array<unsigned, 9> functions{7U, 15U, 44U, 0U, 15U, 41U, 15U, 29U, 45U};
        for (unsigned index = 0; index < instructions.size(); ++index)
            instructions[index] = {vector_word(functions[index], index + 1U, index, index + 1U, element),
                                   RspPipeline::Operation::Cop2};
        const auto code = RspNativeCode::compile(instructions);
        CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
        if (!code)
            continue;
        for (unsigned pattern = 0; pattern < 3U; ++pattern) {
            auto machine = std::make_unique<System>();
            machine->rsp.set_native_execution(false);
            Oracle oracle;
            seed(machine->rsp, oracle, pattern);
            RspNativeState state{&machine->rsp, nullptr, nullptr};
            code->execute(state);
            for (const auto instruction : instructions)
                oracle.execute(instruction.word);
            verify(machine->rsp, oracle);
        }
    }
}
