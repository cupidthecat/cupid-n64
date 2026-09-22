#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <bit>
#include <utility>

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
        const s64 product = static_cast<s64>(std::bit_cast<s16>(left[lane])) * right[lane];
        state.accumulator[lane] = wrap48(product);
    }
}

std::array<u16, 8> apply_scalar(unsigned function, unsigned element, const std::array<u16, 8>& left,
                                const std::array<u16, 8>& right, ScalarState& state) {
    std::array<u16, 8> result{};
    if (function == 0x1dU) {
        for (unsigned lane = 0; lane < result.size(); ++lane) {
            if (element == 8U)
                result[lane] = acc_high(state.accumulator[lane]);
            else if (element == 9U)
                result[lane] = acc_mid(state.accumulator[lane]);
            else if (element == 10U)
                result[lane] = acc_low(state.accumulator[lane]);
        }
        return result;
    }

    const u8 old_vcol = state.vcol;
    const u8 old_vcoh = state.vcoh;
    for (unsigned lane = 0; lane < result.size(); ++lane) {
        const u16 left_u = left[lane];
        const u16 right_u = right[selected_lane(element, lane)];
        const s16 left_s = std::bit_cast<s16>(left_u);
        const s16 right_s = std::bit_cast<s16>(right_u);
        if (function == 0x13U) {
            if (left_s < 0) {
                const u16 low = static_cast<u16>(0U - right_u);
                state.accumulator[lane] = set_acc_low(state.accumulator[lane], low);
                result[lane] = right_u == 0x8000U ? 0x7fffU : low;
            } else if (left_s > 0) {
                state.accumulator[lane] = set_acc_low(state.accumulator[lane], right_u);
                result[lane] = right_u;
            } else {
                state.accumulator[lane] = set_acc_low(state.accumulator[lane], 0);
                result[lane] = 0;
            }
            continue;
        }

        bool choose_left = false;
        if (function == 0x20U) {
            choose_left =
                left_s < right_s || (left_s == right_s && flag(old_vcol, lane) && flag(old_vcoh, lane));
        } else if (function == 0x21U) {
            choose_left = left_u == right_u && !flag(old_vcoh, lane);
        } else if (function == 0x22U) {
            choose_left = left_u != right_u || flag(old_vcoh, lane);
        } else if (function == 0x23U) {
            choose_left =
                left_s > right_s || (left_s == right_s && (!flag(old_vcol, lane) || !flag(old_vcoh, lane)));
        } else if (function == 0x27U) {
            choose_left = flag(state.vccl, lane);
        }
        result[lane] = choose_left ? left_u : right_u;
        state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
        if (function >= 0x20U && function <= 0x23U)
            set_flag(state.vccl, lane, choose_left);
    }

    if (function >= 0x20U && function <= 0x23U) {
        state.vcch = 0;
        state.vcol = state.vcoh = 0;
    } else if (function == 0x27U) {
        state.vcol = state.vcoh = 0;
    }
    return result;
}

struct Observation {
    std::array<u16, 8> result{};
    std::array<u16, 8> acc_high{};
    std::array<u16, 8> acc_mid{};
    std::array<u16, 8> acc_low{};
    u16 vco{};
    u16 vcc{};
    u8 vce{};
};

Observation execute(unsigned function, unsigned element, unsigned destination, const std::array<u16, 8>& left,
                    const std::array<u16, 8>& right, u16 vco, u16 vcc, u8 vce, bool shared_source = false) {
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
    const unsigned vt = shared_source ? 1U : 2U;
    emit(vector_op(function, destination, 1, vt, element));
    emit(vector_store(destination, 0, 2));
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

ScalarState initial_state(u16 vco, u16 vcc, u8 vce) {
    ScalarState state{};
    state.vcol = static_cast<u8>(vco);
    state.vcoh = static_cast<u8>(vco >> 8U);
    state.vccl = static_cast<u8>(vcc);
    state.vcch = static_cast<u8>(vcc >> 8U);
    state.vce = vce;
    return state;
}

constexpr std::array<u16, 8> edge_left{
    0x7fff, 0x8000, 0xffff, 0x0000, 0x4000, 0xc000, 0x1234, 0xfedc,
};
constexpr std::array<u16, 8> edge_right{
    0x0000, 0xffff, 0x8000, 0x7fff, 0x4001, 0xbfff, 0xedcb, 0x0124,
};

} // namespace

TEST(rsp_vector_sse2_vabs_preserves_wrapped_accumulator_when_signed_result_saturates) {
    constexpr std::array<u16, 8> controls{0xffff, 0xffff, 0x0000, 0x0001, 0x8000, 0x7fff, 0x0000, 0xffff};
    constexpr std::array<u16, 8> inputs{0x8000, 0x7fff, 0x8000, 0x8000, 0x0001, 0xffff, 0x1234, 0x0000};
    constexpr std::array<u16, 8> expected_result{0x7fff, 0x8001, 0x0000, 0x8000,
                                                 0xffff, 0xffff, 0x0000, 0x0000};
    constexpr std::array<u16, 8> expected_low{0x8000, 0x8001, 0x0000, 0x8000, 0xffff, 0xffff, 0x0000, 0x0000};
    const auto observed = execute(0x13, 0, 3, controls, inputs, 0xa55a, 0xc33c, 0x5a);
    check_vector(observed.result, expected_result);
    check_vector(observed.acc_low, expected_low);
    CHECK_EQ(observed.vco, 0xa55aU);
    CHECK_EQ(observed.vcc, 0xc33cU);
    CHECK_EQ(observed.vce, 0x5aU);
}

TEST(rsp_vector_sse2_compare_ties_consume_old_vco_and_publish_exact_vccl) {
    constexpr std::array<u16, 8> left{0, 0, 0, 0, 0x7fff, 0x8000, 0xffff, 0x0001};
    constexpr std::array<u16, 8> right{0, 0, 0, 0, 0x8000, 0x7fff, 0x0001, 0xffff};
    constexpr std::array<unsigned, 4> functions{0x20, 0x21, 0x22, 0x23};
    constexpr std::array<u8, 4> expected_vccl{0x61, 0x0a, 0xf5, 0x9e};
    constexpr std::array<std::array<u16, 8>, 4> expected_result{{
        {0, 0, 0, 0, 0x8000, 0x8000, 0xffff, 0xffff},
        {0, 0, 0, 0, 0x8000, 0x7fff, 0x0001, 0xffff},
        {0, 0, 0, 0, 0x7fff, 0x8000, 0xffff, 0x0001},
        {0, 0, 0, 0, 0x7fff, 0x7fff, 0x0001, 0x0001},
    }};
    for (unsigned index = 0; index < functions.size(); ++index) {
        const auto observed = execute(functions[index], 0, 3, left, right, 0x0503, 0xc300, 0x5a);
        check_vector(observed.result, expected_result[index]);
        CHECK_EQ(observed.vco, 0U);
        CHECK_EQ(observed.vcc, expected_vccl[index]);
        CHECK_EQ(observed.vce, 0x5aU);
    }
}

TEST(rsp_vector_sse2_vmrg_uses_vccl_without_disturbing_vcc_or_vce) {
    constexpr std::array<u16, 8> left{0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007};
    constexpr std::array<u16, 8> right{0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007};
    constexpr std::array<u16, 8> expected{0x1000, 0x2001, 0x1002, 0x2003, 0x2004, 0x1005, 0x2006, 0x1007};
    const auto observed = execute(0x27, 0, 3, left, right, 0xa55a, 0xc3a5, 0x69);
    check_vector(observed.result, expected);
    CHECK_EQ(observed.vco, 0U);
    CHECK_EQ(observed.vcc, 0xc3a5U);
    CHECK_EQ(observed.vce, 0x69U);
}

TEST(rsp_vector_sse2_vsaw_reads_each_accumulator_slice_and_zeroes_other_elements) {
    constexpr std::array<u16, 8> left{0x0001, 0xffff, 0x0002, 0xfffe, 0x7fff, 0x8000, 0x0000, 0x1234};
    constexpr std::array<u16, 8> right{0x0001, 0x0001, 0x0002, 0x0002, 0x0001, 0x0001, 0xffff, 0x0010};
    constexpr std::array<u16, 8> high{0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0x0000};
    constexpr std::array<u16, 8> middle{0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0x0001};
    constexpr std::array<u16, 8> low{0x0001, 0xffff, 0x0004, 0xfffc, 0x7fff, 0x8000, 0x0000, 0x2340};
    constexpr std::array<u16, 8> zero{};
    for (const auto& [element, expected] : std::array<std::pair<unsigned, const std::array<u16, 8>*>, 7>{{
             {0, &zero},
             {7, &zero},
             {8, &high},
             {9, &middle},
             {10, &low},
             {11, &zero},
             {15, &zero},
         }}) {
        const auto observed = execute(0x1d, element, 3, left, right, 0xa55a, 0xc33c, 0x5a);
        check_vector(observed.result, *expected);
        check_vector(observed.acc_high, high);
        check_vector(observed.acc_mid, middle);
        check_vector(observed.acc_low, low);
        CHECK_EQ(observed.vco, 0xa55aU);
        CHECK_EQ(observed.vcc, 0xc33cU);
        CHECK_EQ(observed.vce, 0x5aU);
    }
}

TEST(rsp_vector_sse2_simple_compare_slice_matches_scalar_model_for_elements_and_aliases) {
    constexpr u16 vco = 0xa55a;
    constexpr u16 vcc = 0xc33c;
    constexpr u8 vce = 0x5a;
    for (const unsigned function : {0x13U, 0x20U, 0x21U, 0x22U, 0x23U, 0x27U, 0x1dU}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                ScalarState expected = initial_state(vco, vcc, vce);
                seed_accumulator(edge_left, edge_right, expected);
                const auto expected_result = apply_scalar(function, element, edge_left, edge_right, expected);
                const auto observed =
                    execute(function, element, destination, edge_left, edge_right, vco, vcc, vce);
                check_state(observed, expected_result, expected);
            }
        }
    }
}

TEST(rsp_vector_sse2_simple_compare_slice_snapshots_shared_vs_vt_vd_before_writing) {
    constexpr u16 vco = 0xa55a;
    constexpr u16 vcc = 0xc33c;
    constexpr u8 vce = 0x5a;
    for (const unsigned function : {0x13U, 0x20U, 0x21U, 0x22U, 0x23U, 0x27U}) {
        for (unsigned element = 0; element < 16; ++element) {
            ScalarState expected = initial_state(vco, vcc, vce);
            seed_accumulator(edge_left, edge_right, expected);
            const auto expected_result = apply_scalar(function, element, edge_left, edge_left, expected);
            const auto observed = execute(function, element, 1, edge_left, edge_right, vco, vcc, vce, true);
            check_state(observed, expected_result, expected);
        }
    }
}
