#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <bit>

using namespace cupid;

namespace {

constexpr u64 acc_mask = (u64{1} << 48U) - 1U;
constexpr u64 acc_sign = u64{1} << 47U;

constexpr u32 addiu(unsigned rt, unsigned rs, s16 immediate) {
    return (0x09u << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(immediate);
}

constexpr u32 sw(unsigned rt, unsigned rs, s16 immediate) {
    return (0x2bu << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(immediate);
}

constexpr u32 vector_op(unsigned function, unsigned vd, unsigned vs, unsigned vt, unsigned element) {
    return (0x12u << 26) | ((0x10u | (element & 15u)) << 21) | (vt << 16) | (vs << 11) | (vd << 6) |
           (function & 63u);
}

constexpr u32 vector_load(unsigned vt, s8 immediate, unsigned base) {
    return (0x32u << 26) | (base << 21) | (vt << 16) | (0x04u << 11) | (static_cast<u8>(immediate) & 0x7fu);
}

constexpr u32 vector_store(unsigned vt, s8 immediate, unsigned base) {
    return (0x3au << 26) | (base << 21) | (vt << 16) | (0x04u << 11) | (static_cast<u8>(immediate) & 0x7fu);
}

constexpr u32 cfc2(unsigned rt, unsigned rd) {
    return (0x12u << 26) | (0x02u << 21) | (rt << 16) | (rd << 11);
}

constexpr u32 ctc2(unsigned rt, unsigned rd) {
    return (0x12u << 26) | (0x06u << 21) | (rt << 16) | (rd << 11);
}

constexpr u32 break_instruction() {
    return 0x0000'000d;
}

void put_instruction(Rsp& rsp, u32 address, u32 instruction) {
    write_be32(&rsp.memory[0x1000u | (address & 0xffcu)], instruction);
}

void write_vector(Rsp& rsp, u32 address, const std::array<u16, 8>& values) {
    for (unsigned lane = 0; lane < values.size(); ++lane) {
        const u32 offset = (address + lane * 2U) & 0xfffU;
        rsp.memory[offset] = static_cast<u8>(values[lane] >> 8U);
        rsp.memory[(offset + 1U) & 0xfffU] = static_cast<u8>(values[lane]);
    }
}

std::array<u16, 8> read_vector(const Rsp& rsp, u32 address) {
    std::array<u16, 8> values{};
    for (unsigned lane = 0; lane < values.size(); ++lane) {
        const u32 offset = (address + lane * 2U) & 0xfffU;
        values[lane] = static_cast<u16>((static_cast<u16>(rsp.memory[offset]) << 8U) |
                                        rsp.memory[(offset + 1U) & 0xfffU]);
    }
    return values;
}

u32 read_word(const Rsp& rsp, u32 address) {
    const u32 a = address & 0xfffU;
    return (static_cast<u32>(rsp.memory[a]) << 24U) |
           (static_cast<u32>(rsp.memory[(a + 1U) & 0xfffU]) << 16U) |
           (static_cast<u32>(rsp.memory[(a + 2U) & 0xfffU]) << 8U) | rsp.memory[(a + 3U) & 0xfffU];
}

unsigned selected_lane(unsigned element, unsigned lane) {
    lane &= 7U;
    switch (element & 15U) {
    case 0:
    case 1:
        return lane;
    case 2:
        return lane & ~1U;
    case 3:
        return lane | 1U;
    case 4:
    case 5:
    case 6:
    case 7:
        return lane < 4U ? element - 4U : element;
    default:
        return element - 8U;
    }
}

s16 signed16(u16 value) {
    return std::bit_cast<s16>(value);
}

s64 wrap48(s64 value) {
    u64 bits = static_cast<u64>(value) & acc_mask;
    if ((bits & acc_sign) != 0)
        bits |= ~acc_mask;
    return std::bit_cast<s64>(bits);
}

u16 acc_low(s64 value) {
    return static_cast<u16>(static_cast<u64>(value));
}

u16 acc_mid(s64 value) {
    return static_cast<u16>(static_cast<u64>(value) >> 16U);
}

u16 acc_high(s64 value) {
    return static_cast<u16>(static_cast<u64>(value) >> 32U);
}

s64 set_acc_low(s64 value, u16 low) {
    const u64 bits = static_cast<u64>(value) & acc_mask;
    return wrap48(static_cast<s64>((bits & ~u64{0xffff}) | low));
}

bool flag(u8 value, unsigned lane) {
    return ((value >> lane) & 1U) != 0;
}

void set_flag(u8& value, unsigned lane, bool enabled) {
    const u8 bit = static_cast<u8>(1U << lane);
    value = enabled ? static_cast<u8>(value | bit) : static_cast<u8>(value & static_cast<u8>(~bit));
}

struct ScalarState {
    std::array<s64, 8> accumulator{};
    u8 vcol{};
    u8 vcoh{};
    u8 vccl{};
    u8 vcch{};
    u8 vce{};
};

void seed_accumulator(const std::array<u16, 8>& left, const std::array<u16, 8>& right, ScalarState& state) {
    for (unsigned lane = 0; lane < state.accumulator.size(); ++lane) {
        const s64 product = static_cast<s64>(signed16(left[lane])) * right[lane];
        state.accumulator[lane] = wrap48(product);
    }
}

std::array<u16, 8> apply_clip(unsigned function, unsigned element, const std::array<u16, 8>& left,
                              const std::array<u16, 8>& right, ScalarState& state) {
    std::array<u16, 8> result{};
    const u8 old_vcol = state.vcol;
    const u8 old_vcoh = state.vcoh;
    for (unsigned lane = 0; lane < result.size(); ++lane) {
        const u16 left_u = left[lane];
        const u16 right_u = right[selected_lane(element, lane)];
        const s16 left_s = signed16(left_u);
        const s16 right_s = signed16(right_u);

        if (function == 0x24U) {
            u16 value = left_u;
            if (flag(old_vcol, lane)) {
                if (!flag(old_vcoh, lane)) {
                    const u32 full = static_cast<u32>(left_u) + right_u;
                    const u16 sum = static_cast<u16>(full);
                    const bool carry = full > 0xffffU;
                    const bool select = flag(state.vce, lane) ? (sum == 0 || !carry) : (sum == 0 && !carry);
                    set_flag(state.vccl, lane, select);
                }
                value = flag(state.vccl, lane) ? static_cast<u16>(0U - right_u) : left_u;
            } else {
                if (!flag(old_vcoh, lane))
                    set_flag(state.vcch, lane, left_u >= right_u);
                value = flag(state.vcch, lane) ? right_u : left_u;
            }
            result[lane] = value;
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], value);
            continue;
        }

        const bool opposite = (left_s < 0) != (right_s < 0);
        if (function == 0x25U) {
            u16 value{};
            set_flag(state.vcol, lane, opposite);
            if (opposite) {
                const s32 sum = static_cast<s32>(left_s) + right_s;
                set_flag(state.vccl, lane, sum <= 0);
                set_flag(state.vcch, lane, right_s < 0);
                set_flag(state.vcoh, lane, sum != 0 && left_u != static_cast<u16>(~right_u));
                set_flag(state.vce, lane, sum == -1);
                value = flag(state.vccl, lane) ? static_cast<u16>(0U - right_u) : left_u;
            } else {
                const s32 difference = static_cast<s32>(left_s) - right_s;
                set_flag(state.vccl, lane, right_s < 0);
                set_flag(state.vcch, lane, difference >= 0);
                set_flag(state.vcoh, lane, difference != 0 && left_u != static_cast<u16>(~right_u));
                set_flag(state.vce, lane, false);
                value = flag(state.vcch, lane) ? right_u : left_u;
            }
            result[lane] = value;
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], value);
            continue;
        }

        u16 value{};
        if (opposite) {
            set_flag(state.vcch, lane, right_s < 0);
            set_flag(state.vccl, lane, static_cast<s32>(left_s) + right_s + 1 <= 0);
            value = flag(state.vccl, lane) ? static_cast<u16>(~right_u) : left_u;
        } else {
            set_flag(state.vccl, lane, right_s < 0);
            set_flag(state.vcch, lane, static_cast<s32>(left_s) - right_s >= 0);
            value = flag(state.vcch, lane) ? right_u : left_u;
        }
        result[lane] = value;
        state.accumulator[lane] = set_acc_low(state.accumulator[lane], value);
    }

    if (function == 0x24U || function == 0x26U)
        state.vcol = state.vcoh = state.vce = 0;
    return result;
}

std::array<u16, 8> apply_vmrg(unsigned element, const std::array<u16, 8>& left,
                              const std::array<u16, 8>& right, ScalarState& state) {
    std::array<u16, 8> result{};
    for (unsigned lane = 0; lane < result.size(); ++lane) {
        const u16 right_u = right[selected_lane(element, lane)];
        result[lane] = flag(state.vccl, lane) ? left[lane] : right_u;
        state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
    }
    state.vcol = state.vcoh = 0;
    return result;
}

struct Operation {
    unsigned function;
    unsigned element;
    unsigned vd;
    unsigned vs;
    unsigned vt;
};

struct Observation {
    std::array<u16, 8> result{};
    std::array<u16, 8> acc_high{};
    std::array<u16, 8> acc_mid{};
    std::array<u16, 8> acc_low{};
    u16 vco{};
    u16 vcc{};
    u8 vce{};
};

template <std::size_t Count>
Observation execute(const std::array<Operation, Count>& operations, const std::array<u16, 8>& left,
                    const std::array<u16, 8>& right, u16 vco, u16 vcc, u8 vce) {
    System system;
    write_vector(system.rsp, 0x100, left);
    write_vector(system.rsp, 0x110, right);

    u32 pc = 0;
    const auto emit = [&](u32 instruction) {
        put_instruction(system.rsp, pc, instruction);
        pc += 4;
    };
    emit(addiu(1, 0, 0x100));
    emit(addiu(2, 0, 0x300));
    emit(addiu(3, 0, std::bit_cast<s16>(vco)));
    emit(addiu(4, 0, std::bit_cast<s16>(vcc)));
    emit(addiu(5, 0, static_cast<s16>(vce)));
    emit(vector_load(1, 0, 1));
    emit(vector_load(2, 1, 1));
    emit(ctc2(3, 0));
    emit(ctc2(4, 1));
    emit(ctc2(5, 2));
    emit(vector_op(0x05, 7, 1, 2, 0));
    for (const auto& operation : operations)
        emit(vector_op(operation.function, operation.vd, operation.vs, operation.vt, operation.element));
    emit(vector_store(operations.back().vd, 0, 2));
    emit(vector_op(0x1d, 4, 0, 0, 8));
    emit(vector_store(4, 1, 2));
    emit(vector_op(0x1d, 5, 0, 0, 9));
    emit(vector_store(5, 2, 2));
    emit(vector_op(0x1d, 6, 0, 0, 10));
    emit(vector_store(6, 3, 2));
    emit(cfc2(8, 0));
    emit(cfc2(9, 1));
    emit(cfc2(10, 2));
    emit(sw(8, 2, 0x40));
    emit(sw(9, 2, 0x44));
    emit(sw(10, 2, 0x48));
    emit(break_instruction());

    system.rsp.pc = 0;
    system.rsp.write_register(0x10, 1U);
    system.rsp.tick(512);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 3U);

    return {read_vector(system.rsp, 0x300),
            read_vector(system.rsp, 0x310),
            read_vector(system.rsp, 0x320),
            read_vector(system.rsp, 0x330),
            static_cast<u16>(read_word(system.rsp, 0x340)),
            static_cast<u16>(read_word(system.rsp, 0x344)),
            static_cast<u8>(read_word(system.rsp, 0x348))};
}

void check_vector(const std::array<u16, 8>& observed, const std::array<u16, 8>& expected) {
    for (unsigned lane = 0; lane < expected.size(); ++lane)
        CHECK_EQ(observed[lane], expected[lane]);
}

void check_state(const Observation& observed, const std::array<u16, 8>& expected_result,
                 const ScalarState& expected) {
    check_vector(observed.result, expected_result);
    for (unsigned lane = 0; lane < expected.accumulator.size(); ++lane) {
        CHECK_EQ(observed.acc_high[lane], acc_high(expected.accumulator[lane]));
        CHECK_EQ(observed.acc_mid[lane], acc_mid(expected.accumulator[lane]));
        CHECK_EQ(observed.acc_low[lane], acc_low(expected.accumulator[lane]));
    }
    CHECK_EQ(observed.vco, static_cast<u16>(expected.vcol | (static_cast<u16>(expected.vcoh) << 8U)));
    CHECK_EQ(observed.vcc, static_cast<u16>(expected.vccl | (static_cast<u16>(expected.vcch) << 8U)));
    CHECK_EQ(observed.vce, expected.vce);
}

ScalarState initial_state(u16 vco, u16 vcc, u8 vce, const std::array<u16, 8>& left,
                          const std::array<u16, 8>& right) {
    ScalarState state{};
    state.vcol = static_cast<u8>(vco);
    state.vcoh = static_cast<u8>(vco >> 8U);
    state.vccl = static_cast<u8>(vcc);
    state.vcch = static_cast<u8>(vcc >> 8U);
    state.vce = vce;
    seed_accumulator(left, right, state);
    return state;
}

enum class AliasMode { Distinct, VdVs, VdVt, SharedSources, All };

Operation aliased_operation(unsigned function, unsigned element, AliasMode mode) {
    unsigned vd = 3;
    unsigned vt = 2;
    if (mode == AliasMode::VdVs)
        vd = 1;
    if (mode == AliasMode::VdVt)
        vd = 2;
    if (mode == AliasMode::SharedSources)
        vt = 1;
    if (mode == AliasMode::All)
        vd = vt = 1;
    return {function, element, vd, 1, vt};
}

constexpr std::array<u16, 8> edge_left{
    0x0000, 0x0001, 0x7fff, 0x8000, 0xffff, 0x4000, 0xc000, 0x1234,
};
constexpr std::array<u16, 8> edge_right{
    0xffff, 0x0000, 0x8000, 0x7fff, 0x0001, 0xbfff, 0x4001, 0xedcc,
};

} // namespace

TEST(rsp_vector_sse2_clip_ops_match_independent_scalar_oracle_for_elements_and_aliases) {
    constexpr u16 vco = 0x901f;
    constexpr u16 vcc = 0x8010;
    constexpr u8 vce = 0x0c;
    constexpr std::array aliases{AliasMode::Distinct, AliasMode::VdVs, AliasMode::VdVt,
                                 AliasMode::SharedSources, AliasMode::All};
    for (const unsigned function : {0x24U, 0x25U, 0x26U}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const AliasMode alias : aliases) {
                const bool shared = alias == AliasMode::SharedSources || alias == AliasMode::All;
                const auto& clip_right = shared ? edge_left : edge_right;
                ScalarState expected = initial_state(vco, vcc, vce, edge_left, edge_right);
                const auto expected_result = apply_clip(function, element, edge_left, clip_right, expected);
                const auto observed = execute(std::array{aliased_operation(function, element, alias)},
                                              edge_left, edge_right, vco, vcc, vce);
                check_state(observed, expected_result, expected);
            }
        }
    }
}

TEST(rsp_vector_sse2_vch_distinguishes_zero_minus_one_and_nonzero_differences) {
    constexpr std::array<u16, 8> left{0x0001, 0x0000, 0x7fff, 0x8000, 0x0000, 0xffff, 0x7fff, 0x8000};
    constexpr std::array<u16, 8> right{0xffff, 0xffff, 0x8000, 0x7fff, 0x0000, 0xffff, 0x0001, 0xffff};
    constexpr std::array<u16, 8> result{0x0001, 0x0001, 0x8000, 0x8001, 0x0000, 0xffff, 0x0001, 0x8000};
    const auto observed = execute(std::array{Operation{0x25, 0, 3, 1, 2}}, left, right, 0xa55a, 0xc33c, 0x5a);
    check_vector(observed.result, result);
    CHECK_EQ(observed.vco, 0xc00fU);
    CHECK_EQ(observed.vcc, 0x77afU);
    CHECK_EQ(observed.vce, 0x0eU);
}

TEST(rsp_vector_sse2_vcl_preserves_old_vcc_and_handles_zero_carry_and_vce) {
    constexpr std::array<u16, 8> left{0x0000, 0x0001, 0x0001, 0xffff, 0x1234, 0x0005, 0x0004, 0x0001};
    constexpr std::array<u16, 8> right{0x0000, 0xffff, 0x0001, 0x0002, 0xedcc, 0x0004, 0x0005, 0xffff};
    constexpr std::array<u16, 8> result{0x0000, 0x0001, 0xffff, 0xffff, 0x1234, 0x0004, 0x0004, 0xffff};
    const auto observed = execute(std::array{Operation{0x24, 0, 3, 1, 2}}, left, right, 0x901f, 0x8010, 0x0c);
    check_vector(observed.result, result);
    CHECK_EQ(observed.vco, 0U);
    CHECK_EQ(observed.vcc, 0xa015U);
    CHECK_EQ(observed.vce, 0U);
}

TEST(rsp_vector_sse2_vcr_uses_ones_complement_and_exact_signed_boundaries) {
    constexpr std::array<u16, 8> left{0x0000, 0x0001, 0xffff, 0xfffe, 0x0000, 0xffff, 0x7fff, 0x8000};
    constexpr std::array<u16, 8> right{0xffff, 0xffff, 0x0001, 0x0001, 0x0000, 0xffff, 0x0001, 0xffff};
    constexpr std::array<u16, 8> result{0x0000, 0x0001, 0xffff, 0xfffe, 0x0000, 0xffff, 0x0001, 0x8000};
    const auto observed = execute(std::array{Operation{0x26, 0, 3, 1, 2}}, left, right, 0xa55a, 0xc33c, 0x5a);
    check_vector(observed.result, result);
    CHECK_EQ(observed.vco, 0U);
    CHECK_EQ(observed.vcc, 0x73a9U);
    CHECK_EQ(observed.vce, 0U);
    CHECK_EQ(observed.result[0], static_cast<u16>(~right[0]));
    CHECK(observed.result[0] != static_cast<u16>(0U - right[0]));
}

TEST(rsp_vector_sse2_vch_vcl_chain_matches_scalar_flags_and_accumulator) {
    constexpr u16 vco = 0xa55a;
    constexpr u16 vcc = 0xc33c;
    constexpr u8 vce = 0x5a;
    for (const unsigned element : {0U, 3U, 8U, 15U}) {
        ScalarState expected = initial_state(vco, vcc, vce, edge_left, edge_right);
        apply_clip(0x25, element, edge_left, edge_right, expected);
        const auto expected_result = apply_clip(0x24, element, edge_left, edge_right, expected);
        const std::array operations{Operation{0x25, element, 3, 1, 2}, Operation{0x24, element, 3, 1, 2}};
        check_state(execute(operations, edge_left, edge_right, vco, vcc, vce), expected_result, expected);
    }
}

TEST(rsp_vector_sse2_vch_vcl_vmerge_chain_consumes_final_vcc) {
    constexpr u16 vco = 0xa55a;
    constexpr u16 vcc = 0xc33c;
    constexpr u8 vce = 0x5a;
    for (const unsigned element : {0U, 4U, 9U, 15U}) {
        ScalarState expected = initial_state(vco, vcc, vce, edge_left, edge_right);
        apply_clip(0x25, element, edge_left, edge_right, expected);
        apply_clip(0x24, element, edge_left, edge_right, expected);
        const auto expected_result = apply_vmrg(element, edge_left, edge_right, expected);
        const std::array operations{Operation{0x25, element, 3, 1, 2}, Operation{0x24, element, 3, 1, 2},
                                    Operation{0x27, element, 3, 1, 2}};
        check_state(execute(operations, edge_left, edge_right, vco, vcc, vce), expected_result, expected);
    }
}

TEST(rsp_vector_sse2_vcr_vmerge_consumer_chain_matches_scalar_vcc) {
    constexpr u16 vco = 0xa55a;
    constexpr u16 vcc = 0xc33c;
    constexpr u8 vce = 0x5a;
    for (const unsigned element : {0U, 2U, 7U, 12U}) {
        ScalarState expected = initial_state(vco, vcc, vce, edge_left, edge_right);
        apply_clip(0x26, element, edge_left, edge_right, expected);
        const auto expected_result = apply_vmrg(element, edge_left, edge_right, expected);
        const std::array operations{Operation{0x26, element, 3, 1, 2}, Operation{0x27, element, 3, 1, 2}};
        check_state(execute(operations, edge_left, edge_right, vco, vcc, vce), expected_result, expected);
    }
}
