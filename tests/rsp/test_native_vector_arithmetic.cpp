#include "cupid/system.hpp"
#include "test.hpp"

#include <algorithm>
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
    u8 carry_low{0xa5U}, carry_high{0x5aU};
    u8 compare_low{0x5aU}, compare_high{0xa5U};
    u8 compare_extension{0x96U};
    u16 divider_input{}, divider_output{};
    bool divider_high{};

    void execute(u32 word) {
        const unsigned function = word & 63U;
        const unsigned element = (word >> 21U) & 15U;
        const auto left = vectors[(word >> 11U) & 31U];
        const auto right = vectors[(word >> 16U) & 31U];
        auto& result = vectors[(word >> 6U) & 31U];
        if (function == 50U || function == 51U || function == 54U) {
            const unsigned destination_lane = (word >> 11U) & 7U;
            for (unsigned lane = 0; lane < 8U; ++lane)
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | right[selected_lane(element, lane)];
            if (function == 51U) {
                result[destination_lane] = right[selected_lane(element, destination_lane)];
            } else {
                divider_input = right[element & 7U];
                divider_high = true;
                result[destination_lane] = divider_output;
            }
            return;
        }
        u8 next_carry = 0, next_high = 0, next_compare = 0;
        for (unsigned lane = 0; lane < 8U; ++lane) {
            const u16 a = left[lane], b = right[selected_lane(element, lane)];
            if (function >= 36U && function <= 38U) {
                const auto bit = static_cast<u8>(1U << lane);
                const auto read_flag = [&](u8 flags) { return (flags & bit) != 0; };
                const auto write_flag = [&](u8& flags, bool value) {
                    flags =
                        value ? static_cast<u8>(flags | bit) : static_cast<u8>(flags & static_cast<u8>(~bit));
                };
                const s32 left_signed = std::bit_cast<s16>(a), right_signed = std::bit_cast<s16>(b);
                u16 value = a;
                if (function == 36U) {
                    if (read_flag(carry_low)) {
                        if (!read_flag(carry_high)) {
                            const u32 full = static_cast<u32>(a) + b;
                            const bool zero = static_cast<u16>(full) == 0;
                            const bool no_carry = full <= 65535U;
                            write_flag(compare_low,
                                       read_flag(compare_extension) ? zero || no_carry : zero && no_carry);
                        }
                        value = read_flag(compare_low) ? static_cast<u16>(0U - b) : a;
                    } else {
                        if (!read_flag(carry_high))
                            write_flag(compare_high, a >= b);
                        value = read_flag(compare_high) ? b : a;
                    }
                } else {
                    const bool opposite = (left_signed < 0) != (right_signed < 0);
                    if (opposite) {
                        const s32 sum = left_signed + right_signed;
                        write_flag(compare_low, function == 37U ? sum <= 0 : sum < 0);
                        write_flag(compare_high, right_signed < 0);
                        value = read_flag(compare_low)
                                    ? function == 37U ? static_cast<u16>(0U - b) : static_cast<u16>(~b)
                                    : a;
                        if (function == 37U) {
                            write_flag(carry_high, sum != 0 && a != static_cast<u16>(~b));
                            write_flag(compare_extension, sum == -1);
                        }
                    } else {
                        const s32 difference = left_signed - right_signed;
                        write_flag(compare_low, right_signed < 0);
                        write_flag(compare_high, difference >= 0);
                        value = read_flag(compare_high) ? b : a;
                        if (function == 37U) {
                            write_flag(carry_high, difference != 0 && a != static_cast<u16>(~b));
                            write_flag(compare_extension, false);
                        }
                    }
                    if (function == 37U)
                        write_flag(carry_low, opposite);
                }
                result[lane] = value;
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | value;
            } else if (function == 0x1dU) {
                result[lane] = element >= 8U && element <= 10U
                                   ? static_cast<u16>(accumulator[lane] >> ((10U - element) * 16U))
                                   : 0;
            } else if (function == 19U) {
                const s32 sign = std::bit_cast<s16>(a);
                const s32 target = std::bit_cast<s16>(b);
                const s32 value = sign < 0 ? -target : sign > 0 ? target : 0;
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | static_cast<u16>(value);
                result[lane] = static_cast<u16>(std::clamp(value, -32768, 32767));
            } else if ((function >= 32U && function <= 35U) || function == 39U) {
                const bool low = (carry_low & (1U << lane)) != 0;
                const bool high = (carry_high & (1U << lane)) != 0;
                const s32 left_signed = std::bit_cast<s16>(a), right_signed = std::bit_cast<s16>(b);
                const bool selected = function == 32U ? left_signed < right_signed || (a == b && low && high)
                                      : function == 33U ? a == b && !high
                                      : function == 34U ? a != b || high
                                      : function == 35U
                                          ? left_signed > right_signed || (a == b && !(low && high))
                                          : (compare_low & (1U << lane)) != 0;
                if (selected)
                    next_compare |= static_cast<u8>(1U << lane);
                result[lane] = selected ? a : b;
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | result[lane];
            } else if (function == 16U || function == 17U || function == 20U || function == 21U) {
                s32 value = 0;
                if (function == 16U || function == 17U) {
                    const s32 carry = (carry_low >> lane) & 1U;
                    value = function == 16U
                                ? static_cast<s32>(std::bit_cast<s16>(a)) + std::bit_cast<s16>(b) + carry
                                : static_cast<s32>(std::bit_cast<s16>(a)) - std::bit_cast<s16>(b) - carry;
                    result[lane] = static_cast<u16>(std::clamp(value, -32768, 32767));
                } else {
                    value = function == 20U ? static_cast<s32>(a) + b : static_cast<s32>(a) - b;
                    result[lane] = static_cast<u16>(value);
                    if (function == 20U ? value > 65535 : value < 0)
                        next_carry |= static_cast<u8>(1U << lane);
                    if (function == 21U && value != 0)
                        next_high |= static_cast<u8>(1U << lane);
                }
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | static_cast<u16>(value);
            } else if (function >= 0x28U && function <= 0x2dU) {
                u16 value = function < 0x2aU   ? static_cast<u16>(a & b)
                            : function < 0x2cU ? static_cast<u16>(a | b)
                                               : static_cast<u16>(a ^ b);
                if ((function & 1U) != 0U)
                    value = static_cast<u16>(~value);
                result[lane] = value;
                accumulator[lane] = (accumulator[lane] & ~u64{0xffffU}) | value;
            } else {
                const s64 product = (function == 4U || function == 6U || function == 12U || function == 14U
                                         ? static_cast<s64>(a)
                                         : static_cast<s64>(std::bit_cast<s16>(a))) *
                                    (function == 4U || function == 5U || function == 12U || function == 13U
                                         ? static_cast<s64>(b)
                                         : static_cast<s64>(std::bit_cast<s16>(b)));

                if (function == 11U) {
                    s64 adjusted = std::bit_cast<s32>(static_cast<u32>(accumulator[lane] >> 16U));
                    if (adjusted < 0 && (adjusted & 32) == 0)
                        adjusted += 32;
                    else if (adjusted >= 32 && (adjusted & 32) == 0)
                        adjusted -= 32;
                    accumulator[lane] = (accumulator[lane] & 0xffffU) |
                                        ((static_cast<u64>(adjusted) << 16U) & accumulator_mask);
                    const s64 half = adjusted >> 1U;
                    const s64 clamped = half < -32768 ? -32768 : half > 32767 ? 32767 : half;
                    result[lane] = static_cast<u16>(clamped) & 0xfff0U;
                    continue;
                }
                const u64 shifted = static_cast<u64>(product) << 16U;
                if (function == 3U) {
                    const s64 rounded = product + (product < 0 ? 31 : 0);
                    accumulator[lane] = (static_cast<u64>(rounded) << 16U) & accumulator_mask;
                    const s64 half = rounded >> 1U;
                    const s64 clamped = half < -32768 ? -32768 : half > 32767 ? 32767 : half;
                    result[lane] = static_cast<u16>(clamped) & 0xfff0U;
                    continue;
                }
                const u64 bits =
                    function <= 1U                     ? static_cast<u64>(product * 2 + 0x8000)
                    : function == 4U                   ? static_cast<u64>(product) >> 16U
                    : function == 5U || function == 6U ? static_cast<u64>(product)
                    : function == 7U                   ? shifted
                    : function == 8U || function == 9U ? accumulator[lane] + static_cast<u64>(product * 2)
                    : function == 12U ? accumulator[lane] + (static_cast<u64>(product) >> 16U)
                    : function == 13U || function == 14U ? accumulator[lane] + static_cast<u64>(product)
                                                         : accumulator[lane] + shifted;
                accumulator[lane] = bits & accumulator_mask;
                const s64 upper = signed_accumulator(bits) >> 16U;
                const u16 low_result = upper >= -32768 && upper <= 32767 ? static_cast<u16>(bits)
                                       : upper < 0                       ? 0
                                                                         : 0xffffU;
                const u16 middle_result = upper < 0 ? 0 : upper > 32767 ? 0xffffU : static_cast<u16>(upper);
                result[lane] = function == 4U || function == 6U     ? static_cast<u16>(bits)
                               : function == 5U                     ? static_cast<u16>(bits >> 16U)
                               : function == 1U || function == 9U   ? middle_result
                               : function == 12U || function == 14U ? low_result
                                                                    : saturated_middle(bits);
            }
        }
        if (function == 16U || function == 17U || function == 20U || function == 21U ||
            (function >= 32U && function <= 35U) || function == 39U) {
            carry_low = next_carry;
            carry_high = next_high;
        }
        if (function >= 32U && function <= 35U) {
            compare_low = next_compare;
            compare_high = 0;
        }
        if (function == 36U || function == 38U)
            carry_low = carry_high = compare_extension = 0;
    }
};

void seed(Rsp& rsp, Oracle& oracle, unsigned pattern) {
    constexpr Lanes boundaries{0, 1, 0x7fffU, 0x8000U, 0xffffU, 0x4000U, 0xc000U, 0xfffeU};
    std::vector<u32> words{0x24020200U};
    for (unsigned reg = 0; reg < 32U; ++reg) {
        for (unsigned lane = 0; lane < 8U; ++lane) {
            const u16 value = pattern == 0U || pattern == 3U ? boundaries[(lane + reg + pattern) & 7U]
                              : pattern == 1U || pattern == 4U
                                  ? u16{0x8000U}
                                  : static_cast<u16>((reg * 0x7654U + lane * 0x3211U) ^ 0xa5a5U);
            oracle.vectors[reg][lane] = value;
            write_be16(rsp.memory.data() + 0x200U + reg * 16U + lane * 2U, value);
        }
        words.push_back(vector_memory(0x32U, reg, 0x200U + reg * 16U, 0x200U));
    }
    const u32 initial = vector_word(pattern >= 3U ? 7U : 5U, 30U, 28U, 29U, 0);
    words.push_back(initial);
    oracle.execute(initial);
    if (pattern == 4U) {
        const u32 wrap = vector_word(15U, 30U, 28U, 29U, 0);
        words.push_back(wrap);
        oracle.execute(wrap);
    }
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
    const auto vco = static_cast<u16>((static_cast<unsigned>(oracle.carry_high) << 8U) | oracle.carry_low);
    CHECK_EQ(read_be32(rsp.memory.data() + 0x630U),
             static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(vco))));
    const auto vcc =
        static_cast<u16>((static_cast<unsigned>(oracle.compare_high) << 8U) | oracle.compare_low);
    CHECK_EQ(read_be32(rsp.memory.data() + 0x634U),
             static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(vcc))));
    CHECK_EQ(read_be32(rsp.memory.data() + 0x638U), oracle.compare_extension);
}

} // namespace

TEST(rsp_native_vector_arithmetic_matches_scalar_oracle_for_all_registers_elements_and_aliases) {
    for (unsigned function :
         {0U,  1U,  4U,  5U,  6U,  7U,  8U,  9U,  12U, 13U, 14U, 15U, 16U, 17U, 19U, 20U, 21U, 29U,
          32U, 33U, 34U, 35U, 36U, 37U, 38U, 39U, 40U, 41U, 42U, 43U, 44U, 45U, 50U, 51U, 54U}) {
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
                for (unsigned pattern = 0; pattern < 5U; ++pattern) {
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
    for (const unsigned helper : {3U, 11U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            std::array<RspNativeInstruction, 16> instructions{};
            const std::array<unsigned, 16> functions{7U, 15U, 44U, 0U,  15U, helper, 15U, 41U,
                                                     8U, 13U, 15U, 12U, 14U, 1U,     29U, 45U};
            for (unsigned index = 0; index < instructions.size(); ++index)
                instructions[index] = {vector_word(functions[index], index + 1U, index, index + 1U, element),
                                       RspPipeline::Operation::Cop2};
            const auto code = RspNativeCode::compile(instructions);
            CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
            if (!code)
                continue;
            for (unsigned pattern = 0; pattern < 5U; ++pattern) {
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
}

TEST(rsp_native_vector_overwritten_results_keep_accumulators_consumers_and_block_exit_state) {
    for (unsigned function :
         {0U,  1U,  4U,  5U,  6U,  7U,  8U,  9U,  12U, 13U, 14U, 15U, 16U, 17U, 19U, 20U, 21U, 29U,
          32U, 33U, 34U, 35U, 36U, 37U, 38U, 39U, 40U, 41U, 42U, 43U, 44U, 45U, 50U, 51U, 54U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            for (unsigned consumer = 0; consumer < 3U; ++consumer) {
                std::array<RspNativeInstruction, 16> instructions{};
                for (unsigned index = 0; index < instructions.size(); ++index) {
                    const unsigned source = consumer == 1U && index == 8U ? 22U : (index & 7U) + 1U;
                    const unsigned target = consumer == 2U && index == 8U ? 22U : 31U;
                    instructions[index] = {
                        vector_word(function, index == 15U ? 23U : 22U, source, target, element),
                        RspPipeline::Operation::Cop2};
                }
                const auto code = RspNativeCode::compile(instructions);
                CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
                if (!code)
                    continue;
                for (unsigned pattern = 0; pattern < 5U; ++pattern) {
                    auto machine = std::make_unique<System>();
                    machine->rsp.set_native_execution(false);
                    Oracle oracle;
                    seed(machine->rsp, oracle, pattern);
                    RspNativeState state{&machine->rsp, nullptr, nullptr};
                    code->execute(state);
                    for (auto instruction : instructions)
                        oracle.execute(instruction.word);
                    verify(machine->rsp, oracle);
                }
            }
        }
    }
}

TEST(rsp_native_vector_intermediate_results_remain_visible_to_memory_and_cop2_helpers) {
    for (unsigned kind = 0; kind < 3U; ++kind) {
        for (unsigned element = 0; element < 16U; ++element) {
            for (unsigned pattern = 0; pattern < 5U; ++pattern) {
                std::vector<RspNativeInstruction> instructions{
                    {vector_word(4U, 22U, 1U, 31U, element), RspPipeline::Operation::Cop2}};
                if (kind == 0U) {
                    instructions.push_back(
                        {vector_memory(0x3aU, 22U, 0x400U, 0x200U), RspPipeline::Operation::VectorStore});
                } else if (kind == 1U) {
                    instructions.push_back({0x4801b000U, RspPipeline::Operation::Cop2});
                    instructions.push_back({0x4881b800U, RspPipeline::Operation::Cop2});
                } else {
                    instructions.push_back(
                        {vector_word(3U, 23U, 22U, 31U, element), RspPipeline::Operation::Cop2});
                }
                instructions.push_back(
                    {vector_word(7U, 22U, 3U, 31U, element), RspPipeline::Operation::Cop2});
                instructions.push_back(
                    {vector_word(15U, 22U, 4U, 31U, element), RspPipeline::Operation::Cop2});
                const auto code = RspNativeCode::compile(instructions);
                CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
                if (!code)
                    continue;
                auto machine = std::make_unique<System>();
                machine->rsp.set_native_execution(false);
                Oracle oracle;
                seed(machine->rsp, oracle, pattern);
                oracle.execute(instructions.front().word);
                const auto observed = oracle.vectors[22];
                if (kind == 1U)
                    oracle.vectors[23][0] = observed[0];
                else if (kind == 2U)
                    oracle.execute(instructions[1].word);
                oracle.execute(instructions[instructions.size() - 2U].word);
                oracle.execute(instructions.back().word);
                RspNativeState state{&machine->rsp, nullptr, nullptr};
                code->execute(state);
                if (kind == 0U)
                    for (unsigned lane = 0; lane < 8U; ++lane)
                        CHECK_EQ(read_be16(machine->rsp.memory.data() + 0x400U + lane * 2U), observed[lane]);
                verify(machine->rsp, oracle);
            }
        }
    }
}

TEST(rsp_native_vector_cached_accumulators_reach_helpers_after_fast_and_wrapped_scalar_reads) {
    for (const unsigned address : {0x210U, 0xfffU}) {
        for (unsigned pattern = 0; pattern < 5U; ++pattern) {
            const std::array<RspNativeInstruction, 7> instructions{
                RspNativeInstruction{vector_word(15U, 22U, 1U, 31U, 8U), RspPipeline::Operation::Cop2},
                {vector_word(15U, 22U, 2U, 31U, 9U), RspPipeline::Operation::Cop2},
                {vector_word(15U, 22U, 3U, 31U, 10U), RspPipeline::Operation::Cop2},
                {0x84410000U | (address - 0x200U), RspPipeline::Operation::Lh},
                {vector_word(11U, 23U, 0U, 0U, 0U), RspPipeline::Operation::Cop2},
                {vector_word(15U, 22U, 4U, 31U, 11U), RspPipeline::Operation::Cop2},
                {vector_word(29U, 24U, 0U, 0U, 10U), RspPipeline::Operation::Cop2}};
            const auto code = RspNativeCode::compile(instructions);
            CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
            if (!code)
                continue;
            auto machine = std::make_unique<System>();
            machine->rsp.set_native_execution(false);
            Oracle oracle;
            seed(machine->rsp, oracle, pattern);
            // The loaded scalar target is not consumed by later vector operations.
            std::array<u32, 32> registers{};
            registers[1] = 0x96U;
            registers[2] = 0x200U;
            RspNativeState state{&machine->rsp, registers.data(), machine->rsp.memory.data()};
            code->execute(state);
            for (const auto instruction : instructions)
                if (instruction.operation == RspPipeline::Operation::Cop2)
                    oracle.execute(instruction.word);
            verify(machine->rsp, oracle);
        }
    }
}

TEST(rsp_native_vector_add_and_subtract_consume_every_carry_mask_and_clear_both_carry_flags) {
    for (const unsigned function : {16U, 17U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            const u32 word = vector_word(function, 22U, 1U, 31U, element);
            const std::array instructions{RspNativeInstruction{word, RspPipeline::Operation::Cop2}};
            const auto code = RspNativeCode::compile(instructions);
            CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
            if (!code)
                continue;
            for (unsigned mask = 0; mask < 256U; ++mask) {
                auto machine = std::make_unique<System>();
                machine->rsp.set_native_execution(false);
                Oracle oracle;
                seed(machine->rsp, oracle, mask % 5U);
                oracle.carry_low = static_cast<u8>(mask);
                oracle.carry_high = static_cast<u8>(mask ^ 0xa5U);
                const unsigned flags = mask | (static_cast<unsigned>(oracle.carry_high) << 8U);
                const std::array words{0x34010000U | flags, 0x48c10000U, 13U};
                run(machine->rsp, words);
                RspNativeState state{&machine->rsp, nullptr, nullptr};
                code->execute(state);
                oracle.execute(word);
                verify(machine->rsp, oracle);
            }
        }
    }
}

TEST(rsp_native_vector_carry_results_and_accumulator_slices_remain_coherent_inside_cached_chains) {
    for (unsigned element = 0; element < 16U; ++element) {
        constexpr std::array<unsigned, 16> functions{7U,  15U, 20U, 16U, 15U, 21U, 17U, 8U,
                                                     12U, 20U, 16U, 13U, 21U, 17U, 29U, 45U};
        std::array<RspNativeInstruction, 16> instructions{};
        for (unsigned index = 0; index < instructions.size(); ++index)
            instructions[index] = {vector_word(functions[index], 22U, (index & 7U) + 1U, 31U, element),
                                   RspPipeline::Operation::Cop2};
        const auto code = RspNativeCode::compile(instructions);
        CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
        if (!code)
            continue;
        for (unsigned pattern = 0; pattern < 5U; ++pattern) {
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

TEST(rsp_native_vector_comparison_ties_consume_both_carry_groups_and_preserve_extension_flags) {
    for (unsigned function : {32U, 33U, 34U, 35U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            const u32 word = vector_word(function, 22U, 1U, 31U, element);
            const std::array instructions{RspNativeInstruction{word, RspPipeline::Operation::Cop2}};
            const auto code = RspNativeCode::compile(instructions);
            CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
            if (!code)
                continue;
            auto machine = std::make_unique<System>();
            machine->rsp.set_native_execution(false);
            for (unsigned flags : {0U, 0x00ffU, 0xff00U, 0xffffU, 0xa5a5U, 0x5a5aU, 0xa55aU, 0x5aa5U}) {
                Oracle oracle;
                seed(machine->rsp, oracle, 1U);
                oracle.carry_low = static_cast<u8>(flags);
                oracle.carry_high = static_cast<u8>(flags >> 8U);
                const std::array words{0x34010000U | flags, 0x48c10000U, 13U};
                run(machine->rsp, words);
                RspNativeState state{&machine->rsp, nullptr, nullptr};
                code->execute(state);
                oracle.execute(word);
                verify(machine->rsp, oracle);
            }
        }
    }
}

TEST(rsp_native_vector_merge_consumes_every_comparison_mask_without_changing_either_comparison_group) {
    for (unsigned element = 0; element < 16U; ++element) {
        const u32 word = vector_word(39U, 1U, 1U, 31U, element);
        const std::array instructions{RspNativeInstruction{word, RspPipeline::Operation::Cop2}};
        const auto code = RspNativeCode::compile(instructions);
        CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
        if (!code)
            continue;
        auto machine = std::make_unique<System>();
        machine->rsp.set_native_execution(false);
        for (unsigned mask = 0; mask < 256U; ++mask) {
            Oracle oracle;
            seed(machine->rsp, oracle, 0U);
            oracle.compare_low = static_cast<u8>(mask);
            oracle.compare_high = static_cast<u8>(mask ^ 0xa5U);
            const unsigned flags = mask | (static_cast<unsigned>(oracle.compare_high) << 8U);
            const std::array words{0x34010000U | flags, 0x48c10800U, 13U};
            run(machine->rsp, words);
            RspNativeState state{&machine->rsp, nullptr, nullptr};
            code->execute(state);
            oracle.execute(word);
            verify(machine->rsp, oracle);
        }
    }
}

TEST(rsp_native_vector_comparisons_abs_and_merges_keep_cached_accumulators_and_live_flags_coherent) {
    for (unsigned element = 0; element < 16U; ++element) {
        constexpr std::array<unsigned, 16> functions{7U,  15U, 20U, 32U, 39U, 19U, 21U, 35U,
                                                     39U, 8U,  33U, 39U, 34U, 39U, 29U, 45U};
        std::array<RspNativeInstruction, 16> instructions{};
        for (unsigned index = 0; index < instructions.size(); ++index)
            instructions[index] = {
                vector_word(functions[index], index % 3U == 0U ? 1U : 22U, (index & 7U) + 1U, 31U, element),
                RspPipeline::Operation::Cop2};
        const auto code = RspNativeCode::compile(instructions);
        CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
        if (!code)
            continue;
        for (unsigned pattern = 0; pattern < 5U; ++pattern) {
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

TEST(rsp_native_partial_vector_writes_preserve_live_lanes_from_previous_full_destinations) {
    for (unsigned function : {50U, 51U, 54U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            for (unsigned lane = 0; lane < 8U; ++lane) {
                const std::array instructions{
                    RspNativeInstruction{vector_word(7U, 22U, 2U, 3U, element), RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(15U, 22U, 2U, 3U, element),
                                         RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(15U, 22U, 2U, 3U, element),
                                         RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(function, 22U, lane | 24U, 31U, element),
                                         RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(45U, 21U, 22U, 29U, element),
                                         RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(29U, 20U, 0U, 0U, 9U), RspPipeline::Operation::Cop2}};
                const auto code = RspNativeCode::compile(instructions);
                CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
                if (!code)
                    continue;
                for (unsigned pattern : {0U, 4U}) {
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
    }
}

TEST(rsp_native_divider_high_instructions_snapshot_aliases_and_share_the_input_output_latches) {
    for (unsigned high_function : {50U, 54U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            auto machine = std::make_unique<System>();
            auto& rsp = machine->rsp;
            rsp.set_native_execution(false);
            for (unsigned lane = 0; lane < 8U; ++lane) {
                write_be16(rsp.memory.data() + 0x200U + lane * 2U, 1U);
                write_be16(rsp.memory.data() + 0x210U + lane * 2U, 0U);
            }
            for (unsigned lane = 0; lane < 8U; ++lane) {
                const std::array seed_words{0x24020200U,
                                            vector_memory(0x32U, 2U, 0x200U, 0x200U),
                                            vector_memory(0x32U, 3U, 0x210U, 0x200U),
                                            vector_memory(0x32U, 4U, 0x210U, 0x200U),
                                            vector_memory(0x32U, 5U, 0x210U, 0x200U),
                                            vector_memory(0x32U, 6U, 0x200U, 0x200U),
                                            vector_memory(0x32U, 7U, 0x210U, 0x200U),
                                            vector_word(48U, 1U, 0U, 3U, 0U),
                                            13U};
                run(rsp, seed_words);
                const unsigned de = lane | 24U;
                const std::array instructions{
                    RspNativeInstruction{vector_word(high_function, 2U, de, 2U, element),
                                         RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(49U, 4U, de, 3U, element), RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(high_function, 5U, de, 3U, element),
                                         RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(49U, 6U, de, 6U, element), RspPipeline::Operation::Cop2},
                    RspNativeInstruction{vector_word(high_function, 7U, de, 3U, element),
                                         RspPipeline::Operation::Cop2}};
                const auto code = RspNativeCode::compile(instructions);
                CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
                if (!code)
                    continue;
                RspNativeState state{&rsp, nullptr, nullptr};
                code->execute(state);
                std::vector<u32> stores{0x24020400U};
                for (unsigned reg : {2U, 4U, 5U, 6U, 7U})
                    stores.push_back(vector_memory(0x3aU, reg, 0x400U + reg * 16U, 0x400U));
                stores.push_back(13U);
                run(rsp, stores);
                // Reciprocals of zero, 65536, and one expose both halves of the shared latch.
                for (unsigned observed = 0; observed < 8U; ++observed) {
                    CHECK_EQ(read_be16(rsp.memory.data() + 0x420U + observed * 2U),
                             observed == lane ? 0x7fffU : 1U);
                    CHECK_EQ(read_be16(rsp.memory.data() + 0x440U + observed * 2U),
                             observed == lane ? 0x7fffU : 0U);
                    CHECK_EQ(read_be16(rsp.memory.data() + 0x450U + observed * 2U), 0U);
                    CHECK_EQ(read_be16(rsp.memory.data() + 0x460U + observed * 2U),
                             observed == lane ? 0xc000U : 1U);
                    CHECK_EQ(read_be16(rsp.memory.data() + 0x470U + observed * 2U),
                             observed == lane ? 0x7fffU : 0U);
                }
            }
        }
    }
}

namespace {
void seed_clip_flags(Rsp& rsp, Oracle& oracle, const std::array<u8, 5>& flags) {
    oracle.carry_low = flags[0];
    oracle.carry_high = flags[1];
    oracle.compare_low = flags[2];
    oracle.compare_high = flags[3];
    oracle.compare_extension = flags[4];
    const std::array words{0x34010000U | flags[0] | (static_cast<u32>(flags[1]) << 8U),
                           0x48c10000U,
                           0x34010000U | flags[2] | (static_cast<u32>(flags[3]) << 8U),
                           0x48c10800U,
                           0x34010000U | flags[4],
                           0x48c11000U,
                           13U};
    run(rsp, words);
}
} // namespace

TEST(rsp_native_vector_clipping_consumes_every_lane_flag_combination_and_preserves_other_slices) {
    for (unsigned function : {36U, 37U, 38U}) {
        for (unsigned element = 0; element < 16U; ++element) {
            const u32 word = vector_word(function, element % 3U + 1U, 1U, 2U, element);
            const std::array instructions{RspNativeInstruction{word, RspPipeline::Operation::Cop2}};
            const auto code = RspNativeCode::compile(instructions);
            CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
            if (!code)
                continue;
            for (unsigned pattern = 0; pattern < 32U; ++pattern) {
                auto machine = std::make_unique<System>();
                machine->rsp.set_native_execution(false);
                Oracle oracle;
                seed(machine->rsp, oracle, pattern % 5U);
                std::array<u8, 5> flags{};
                for (unsigned lane = 0; lane < 8U; ++lane)
                    for (unsigned group = 0; group < flags.size(); ++group)
                        flags[group] |= static_cast<u8>((((pattern + lane * 13U) >> group) & 1U) << lane);
                seed_clip_flags(machine->rsp, oracle, flags);
                RspNativeState state{&machine->rsp, nullptr, nullptr};
                code->execute(state);
                oracle.execute(word);
                verify(machine->rsp, oracle);
            }
        }
    }
}

TEST(rsp_native_vector_clipping_keeps_accumulators_and_flags_across_compiled_chains) {
    for (unsigned element = 0; element < 16U; ++element) {
        constexpr std::array<unsigned, 16> functions{7U,  15U, 15U, 37U, 36U, 8U,  15U, 38U,
                                                     39U, 29U, 21U, 20U, 7U,  15U, 36U, 29U};
        std::array<RspNativeInstruction, 16> instructions{};
        for (unsigned index = 0; index < instructions.size(); ++index)
            instructions[index] = {
                vector_word(functions[index], index % 3U == 0U ? 1U : 22U, (index & 7U) + 1U, 31U, element),
                RspPipeline::Operation::Cop2};
        const auto code = RspNativeCode::compile(instructions);
        CHECK_EQ(static_cast<bool>(code), RspNativeCode::available());
        if (!code)
            continue;
        for (unsigned pattern = 0; pattern < 5U; ++pattern) {
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