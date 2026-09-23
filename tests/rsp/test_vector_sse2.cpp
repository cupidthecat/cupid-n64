#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <bit>
#include <limits>

using namespace cupid;

namespace {

constexpr u64 acc_mask = (u64{1} << 48U) - 1U;
constexpr u64 acc_sign = u64{1} << 47U;
constexpr unsigned no_seed = 0xffU;

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
    if ((bits & acc_sign) != 0) {
        bits |= ~acc_mask;
    }
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

s64 set_acc_mid(s64 value, u16 middle) {
    const u64 bits = static_cast<u64>(value) & acc_mask;
    return wrap48(static_cast<s64>((bits & ~(u64{0xffff} << 16U)) | (static_cast<u64>(middle) << 16U)));
}

s64 set_acc_high(s64 value, u16 high) {
    const u64 bits = static_cast<u64>(value) & acc_mask;
    return wrap48(static_cast<s64>((bits & ~(u64{0xffff} << 32U)) | (static_cast<u64>(high) << 32U)));
}

u16 saturate_accumulator(s64 value, bool middle_slice, u16 negative, u16 positive) {
    const u16 high = acc_high(value);
    const u16 middle = acc_mid(value);
    if (std::bit_cast<s16>(high) < 0) {
        if (high != 0xffffU || std::bit_cast<s16>(middle) >= 0) {
            return negative;
        }
    } else if (high != 0 || std::bit_cast<s16>(middle) < 0) {
        return positive;
    }
    return middle_slice ? middle : acc_low(value);
}

u16 clamp_signed(s32 value) {
    if (value > std::numeric_limits<s16>::max()) {
        return 0x7fffU;
    }
    if (value < std::numeric_limits<s16>::min()) {
        return 0x8000U;
    }
    return std::bit_cast<u16>(static_cast<s16>(value));
}

void set_flag(u8& flags, unsigned lane, bool value) {
    const u8 bit = static_cast<u8>(1U << lane);
    flags = value ? static_cast<u8>(flags | bit) : static_cast<u8>(flags & static_cast<u8>(~bit));
}

struct OracleState {
    std::array<s64, 8> accumulator{};
    u8 vcol{};
    u8 vcoh{};
    u8 vccl{};
    u8 vcch{};
    u8 vce{};
};

std::array<u16, 8> apply_oracle(unsigned function, unsigned element, const std::array<u16, 8>& left,
                                const std::array<u16, 8>& right, OracleState& state) {
    std::array<u16, 8> result{};
    for (unsigned lane = 0; lane < result.size(); ++lane) {
        const u16 left_u = left[lane];
        const u16 right_u = right[selected_lane(element, lane)];
        const s32 left_s = std::bit_cast<s16>(left_u);
        const s32 right_s = std::bit_cast<s16>(right_u);
        switch (function) {
        case 0x00:
        case 0x01: {
            const s64 product = static_cast<s64>(left_s) * right_s;
            state.accumulator[lane] = wrap48(product * 2 + 0x8000);
            if (function == 0x00) {
                result[lane] = saturate_accumulator(state.accumulator[lane], true, 0x8000, 0x7fff);
            } else if (std::bit_cast<s16>(acc_high(state.accumulator[lane])) < 0) {
                result[lane] = 0;
            } else if (std::bit_cast<s16>(acc_mid(state.accumulator[lane])) < 0) {
                result[lane] = 0xffff;
            } else {
                result[lane] = acc_mid(state.accumulator[lane]);
            }
            break;
        }
        case 0x04: {
            const u32 product = static_cast<u32>(left_u) * right_u;
            state.accumulator[lane] = wrap48(static_cast<s64>(product >> 16U));
            result[lane] = acc_low(state.accumulator[lane]);
            break;
        }
        case 0x05: {
            const s64 product = static_cast<s64>(left_s) * right_u;
            state.accumulator[lane] = wrap48(product);
            result[lane] = acc_mid(state.accumulator[lane]);
            break;
        }
        case 0x06: {
            const s64 product = static_cast<s64>(left_u) * right_s;
            state.accumulator[lane] = wrap48(product);
            result[lane] = acc_low(state.accumulator[lane]);
            break;
        }
        case 0x07: {
            const s64 product = static_cast<s64>(left_s) * right_s;
            state.accumulator[lane] = wrap48(product * 0x1'0000);
            result[lane] = saturate_accumulator(state.accumulator[lane], true, 0x8000, 0x7fff);
            break;
        }
        case 0x08:
        case 0x09: {
            const s64 product = static_cast<s64>(left_s) * right_s * 2;
            state.accumulator[lane] = wrap48(state.accumulator[lane] + product);
            if (function == 0x08) {
                result[lane] = saturate_accumulator(state.accumulator[lane], true, 0x8000, 0x7fff);
            } else if (std::bit_cast<s16>(acc_high(state.accumulator[lane])) < 0) {
                result[lane] = 0;
            } else if (acc_high(state.accumulator[lane]) != 0 ||
                       std::bit_cast<s16>(acc_mid(state.accumulator[lane])) < 0) {
                result[lane] = 0xffff;
            } else {
                result[lane] = acc_mid(state.accumulator[lane]);
            }
            break;
        }
        case 0x0c: {
            const u32 product = static_cast<u32>(left_u) * right_u;
            state.accumulator[lane] = wrap48(state.accumulator[lane] + static_cast<s64>(product >> 16U));
            result[lane] = saturate_accumulator(state.accumulator[lane], false, 0, 0xffff);
            break;
        }
        case 0x0d: {
            const s64 product = static_cast<s64>(left_s) * right_u;
            state.accumulator[lane] = wrap48(state.accumulator[lane] + product);
            result[lane] = saturate_accumulator(state.accumulator[lane], true, 0x8000, 0x7fff);
            break;
        }
        case 0x0e: {
            const s64 product = static_cast<s64>(left_u) * right_s;
            state.accumulator[lane] = wrap48(state.accumulator[lane] + product);
            result[lane] = saturate_accumulator(state.accumulator[lane], false, 0, 0xffff);
            break;
        }
        case 0x0f: {
            const s64 upper = state.accumulator[lane] >> 16U;
            const s64 product = static_cast<s64>(left_s) * right_s;
            const s64 combined = upper + product;
            state.accumulator[lane] =
                set_acc_high(state.accumulator[lane], static_cast<u16>(static_cast<u64>(combined) >> 16U));
            state.accumulator[lane] = set_acc_mid(state.accumulator[lane], static_cast<u16>(combined));
            result[lane] = saturate_accumulator(state.accumulator[lane], true, 0x8000, 0x7fff);
            break;
        }
        case 0x10: {
            const s32 carry = static_cast<s32>((state.vcol >> lane) & 1U);
            const s32 sum = left_s + right_s + carry;
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], static_cast<u16>(sum));
            result[lane] = clamp_signed(sum);
            break;
        }
        case 0x11: {
            const s32 borrow = static_cast<s32>((state.vcol >> lane) & 1U);
            const s32 difference = left_s - right_s - borrow;
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], static_cast<u16>(difference));
            result[lane] = clamp_signed(difference);
            break;
        }
        case 0x14: {
            const u32 sum = static_cast<u32>(left_u) + right_u;
            result[lane] = static_cast<u16>(sum);
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            set_flag(state.vcol, lane, sum > 0xffffU);
            break;
        }
        case 0x15: {
            result[lane] = static_cast<u16>(left_u - right_u);
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            set_flag(state.vcol, lane, left_u < right_u);
            set_flag(state.vcoh, lane, result[lane] != 0);
            break;
        }
        case 0x28:
            result[lane] = static_cast<u16>(left_u & right_u);
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            break;
        case 0x29:
            result[lane] = static_cast<u16>(~(left_u & right_u));
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            break;
        case 0x2a:
            result[lane] = static_cast<u16>(left_u | right_u);
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            break;
        case 0x2b:
            result[lane] = static_cast<u16>(~(left_u | right_u));
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            break;
        case 0x2c:
            result[lane] = static_cast<u16>(left_u ^ right_u);
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            break;
        case 0x2d:
            result[lane] = static_cast<u16>(~(left_u ^ right_u));
            state.accumulator[lane] = set_acc_low(state.accumulator[lane], result[lane]);
            break;
        default:
            break;
        }
    }

    if (function == 0x10 || function == 0x11) {
        state.vcol = 0;
        state.vcoh = 0;
    } else if (function == 0x14) {
        state.vcoh = 0;
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
    u16 vce{};
};

Observation execute(unsigned function, unsigned element, unsigned destination, const std::array<u16, 8>& left,
                    const std::array<u16, 8>& right, unsigned seed_function, u16 vco, u16 vcc, u8 vce,
                    bool shared_source = false) {
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
    const unsigned vt = shared_source ? 1U : 2U;
    if (seed_function != no_seed) {
        emit(vector_op(seed_function, 7, 1, vt, element));
    }
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
    system.rsp.write_register(0x10, 1U << 0);
    system.rsp.tick(512);
    CHECK((system.rsp.read_register(0x10) & 1U) != 0);

    return {read_vector(system.rsp, 0x300),
            read_vector(system.rsp, 0x310),
            read_vector(system.rsp, 0x320),
            read_vector(system.rsp, 0x330),
            static_cast<u16>(read_word(system.rsp, 0x340)),
            static_cast<u16>(read_word(system.rsp, 0x344)),
            static_cast<u16>(read_word(system.rsp, 0x348))};
}

void check_observation(const Observation& observed, const std::array<u16, 8>& expected_result,
                       const OracleState& expected) {
    for (unsigned lane = 0; lane < expected_result.size(); ++lane) {
        CHECK_EQ(observed.result[lane], expected_result[lane]);
        CHECK_EQ(observed.acc_high[lane], acc_high(expected.accumulator[lane]));
        CHECK_EQ(observed.acc_mid[lane], acc_mid(expected.accumulator[lane]));
        CHECK_EQ(observed.acc_low[lane], acc_low(expected.accumulator[lane]));
    }
    CHECK_EQ(observed.vco, static_cast<u16>(expected.vcol | (static_cast<u16>(expected.vcoh) << 8U)));
    CHECK_EQ(observed.vcc, static_cast<u16>(expected.vccl | (static_cast<u16>(expected.vcch) << 8U)));
    CHECK_EQ(observed.vce, expected.vce);
}

OracleState initial_state(u16 vco, u16 vcc, u8 vce) {
    OracleState state{};
    state.vcol = static_cast<u8>(vco);
    state.vcoh = static_cast<u8>(vco >> 8U);
    state.vccl = static_cast<u8>(vcc);
    state.vcch = static_cast<u8>(vcc >> 8U);
    state.vce = vce;
    return state;
}

void check_case(unsigned function, unsigned element, unsigned destination, const std::array<u16, 8>& left,
                const std::array<u16, 8>& right, unsigned seed_function, u16 vco, u16 vcc, u8 vce,
                bool shared_source = false) {
    const auto& selected_right = shared_source ? left : right;
    OracleState expected = initial_state(vco, vcc, vce);
    if (seed_function != no_seed) {
        (void)apply_oracle(seed_function, element, left, selected_right, expected);
    }
    const auto expected_result = apply_oracle(function, element, left, selected_right, expected);
    const auto observed =
        execute(function, element, destination, left, right, seed_function, vco, vcc, vce, shared_source);
    check_observation(observed, expected_result, expected);
}

constexpr std::array<u16, 8> edge_left{
    0x7fff, 0x8000, 0xffff, 0x0000, 0x4000, 0xc000, 0x1234, 0xfedc,
};
constexpr std::array<u16, 8> edge_right{
    0x0000, 0xffff, 0x8000, 0x7fff, 0x4001, 0xbfff, 0xedcb, 0x0124,
};

} // namespace

TEST(rsp_vector_sse2_vadd_vsub_match_signed_carry_arithmetic) {
    for (const unsigned function : {0x10U, 0x11U}) {
        for (const u8 vcol : {u8{0x00}, u8{0xff}, u8{0x55}, u8{0xaa}, u8{0x81}}) {
            const u16 vco = static_cast<u16>(vcol | (u16{0x5a} << 8U));
            for (unsigned element = 0; element < 16; ++element) {
                for (const unsigned destination : {3U, 1U, 2U}) {
                    check_case(function, element, destination, edge_left, edge_right, 0x07, vco, 0xc33c,
                               0x5a);
                }
            }
        }
    }
}

TEST(rsp_vector_sse2_vadd_vsub_saturate_after_carry_at_both_signed_endpoints) {
    constexpr std::array<u16, 8> boundaries{0x8000, 0x8001, 0xfffe, 0xffff, 0x0000, 0x0001, 0x7ffe, 0x7fff};
    for (const unsigned function : {0x10U, 0x11U}) {
        for (unsigned rotation = 0; rotation < boundaries.size(); ++rotation) {
            std::array<u16, 8> right{};
            for (unsigned lane = 0; lane < right.size(); ++lane)
                right[lane] = boundaries[(lane + rotation) & 7U];
            for (const u16 vco : {u16{0xff00}, u16{0xffff}}) {
                for (const unsigned destination : {1U, 2U, 3U})
                    check_case(function, 0, destination, boundaries, right, 0x07, vco, 0xc33c, 0x5a);
            }
        }
    }
}

TEST(rsp_vector_sse2_carry_ops_match_independent_flags_and_accumulator_oracle) {
    for (const unsigned function : {0x14U, 0x15U}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                check_case(function, element, destination, edge_left, edge_right, 0x07, 0xa55a, 0xc33c, 0x5a);
            }
        }
    }
}

TEST(rsp_vector_sse2_logic_preserves_upper_accumulator_and_control_flags) {
    for (const unsigned function : {0x28U, 0x29U, 0x2aU, 0x2bU, 0x2cU, 0x2dU}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                check_case(function, element, destination, edge_left, edge_right, 0x07, 0xa55a, 0xc33c, 0x5a);
            }
        }
    }
}

TEST(rsp_vector_sse2_multiply_families_match_mixed_signedness_oracle) {
    for (const unsigned function : {0x00U, 0x01U, 0x04U, 0x05U, 0x06U, 0x07U}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                check_case(function, element, destination, edge_left, edge_right, no_seed, 0xa55a, 0xc33c,
                           0x5a);
            }
        }
    }
}

TEST(rsp_vector_sse2_accumulate_families_match_48_bit_oracle) {
    for (const unsigned function : {0x08U, 0x09U, 0x0cU, 0x0dU, 0x0eU, 0x0fU}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                check_case(function, element, destination, edge_left, edge_right, 0x07, 0xa55a, 0xc33c, 0x5a);
            }
        }
    }
}

TEST(rsp_vector_sse2_vmadh_wraps_the_full_48_bit_accumulator) {
    constexpr std::array<u16, 8> minimums{
        0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
    };
    for (const unsigned destination : {3U, 1U, 2U}) {
        check_case(0x0f, 0, destination, minimums, minimums, 0x07, 0xa55a, 0xc33c, 0x5a);
    }
}

TEST(rsp_vector_sse2_reads_shuffled_operands_when_both_sources_and_destination_are_the_same_register) {
    for (const unsigned function :
         {0x00U, 0x01U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0cU, 0x0dU, 0x0eU,
          0x0fU, 0x10U, 0x11U, 0x14U, 0x15U, 0x28U, 0x29U, 0x2aU, 0x2bU, 0x2cU, 0x2dU}) {
        for (unsigned element = 0; element < 16; ++element)
            check_case(function, element, 1, edge_left, edge_right, 0x05, 0xa55a, 0xc33c, 0x5a, true);
    }
}

TEST(rsp_vector_sse2_accumulator_carries_cross_both_slice_boundaries_and_wrap_the_sign) {
    System system;
    std::array<u16, 8> lanes{};
    const std::array<u16, 4> values{0x8000U, 0x0100U, 1U, 0xffffU};
    for (unsigned index = 0; index < values.size(); ++index) {
        lanes.fill(values[index]);
        write_vector(system.rsp, 0x100U + index * 16U, lanes);
    }
    u32 pc = 0;
    const auto emit = [&](u32 instruction) {
        put_instruction(system.rsp, pc, instruction);
        pc += 4;
    };
    emit(addiu(1, 0, 0x100));
    emit(addiu(2, 0, 0x400));
    for (unsigned index = 0; index < values.size(); ++index)
        emit(vector_load(index + 1U, static_cast<s8>(index), 1));
    unsigned output = 0;
    const auto snapshot = [&] {
        for (unsigned slice = 0; slice < 3; ++slice) {
            emit(vector_op(0x1d, 20, 0, 0, 8U + slice));
            emit(vector_store(20, static_cast<s8>(output++), 2));
        }
    };
    emit(vector_op(0x04, 7, 4, 4, 0)); // 0xfffe in the low accumulator slice.
    emit(vector_op(0x0c, 7, 2, 2, 0)); // Add one.
    snapshot();
    emit(vector_op(0x0c, 7, 2, 2, 0)); // Carry from low into middle.
    snapshot();
    emit(vector_op(0x07, 7, 1, 1, 0)); // 0x400000000000.
    emit(vector_op(0x0f, 7, 1, 1, 0)); // Wrap to the most negative 48-bit value.
    emit(vector_op(0x0e, 7, 3, 4, 0)); // Subtract one, reaching 0x7fffffffffff.
    snapshot();
    emit(vector_op(0x0c, 7, 2, 2, 0)); // Carry through low and middle and wrap high.
    snapshot();
    emit(break_instruction());
    system.rsp.write_register(0x10, 1U);
    system.rsp.tick(512);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 3U);

    constexpr std::array<u64, 4> expected{0xffffULL, 0x10000ULL, 0x7fffffffffffULL, 0x800000000000ULL};
    for (unsigned sample = 0; sample < expected.size(); ++sample) {
        for (unsigned slice = 0; slice < 3; ++slice) {
            const auto observed = read_vector(system.rsp, 0x400U + (sample * 3U + slice) * 16U);
            for (const u16 value : observed)
                CHECK_EQ(value, static_cast<u16>(expected[sample] >> ((2U - slice) * 16U)));
        }
    }
}
