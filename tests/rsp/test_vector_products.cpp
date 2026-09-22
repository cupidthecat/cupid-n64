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

unsigned selected_lane(unsigned element, unsigned lane) {
    if (element < 2U)
        return lane;
    if (element < 4U)
        return (lane & ~1U) | (element & 1U);
    if (element < 8U)
        return lane < 4U ? element - 4U : element;
    return element - 8U;
}

s64 wrap_accumulator(s64 value) {
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

s16 acc_mid_signed(s64 value) {
    return std::bit_cast<s16>(acc_mid(value));
}

s16 acc_high_signed(s64 value) {
    return std::bit_cast<s16>(acc_high(value));
}

u16 signed_saturation(s64 accumulator) {
    if (acc_high_signed(accumulator) < 0) {
        if (acc_high(accumulator) != 0xffffU || acc_mid_signed(accumulator) >= 0)
            return 0x8000U;
    } else if (acc_high(accumulator) != 0U || acc_mid_signed(accumulator) < 0) {
        return 0x7fffU;
    }
    return acc_mid(accumulator);
}

u16 unsigned_vmulf_result(s64 accumulator) {
    if (acc_high_signed(accumulator) < 0)
        return 0;
    if (acc_mid_signed(accumulator) < 0)
        return 0xffffU;
    return acc_mid(accumulator);
}

u16 unsigned_vmacf_result(s64 accumulator) {
    if (acc_high_signed(accumulator) < 0)
        return 0;
    if (acc_high(accumulator) != 0U || acc_mid_signed(accumulator) < 0)
        return 0xffffU;
    return acc_mid(accumulator);
}

struct Observation {
    std::array<u16, 8> result{};
    std::array<u16, 8> high{};
    std::array<u16, 8> mid{};
    std::array<u16, 8> low{};
};

Observation execute(unsigned function, unsigned element, unsigned destination, const std::array<u16, 8>& left,
                    const std::array<u16, 8>& right, const std::array<u16, 8>& seed_left,
                    const std::array<u16, 8>& seed_right) {
    System system;
    write_vector(system.rsp, 0x100U, left);
    write_vector(system.rsp, 0x110U, right);
    write_vector(system.rsp, 0x120U, seed_left);
    write_vector(system.rsp, 0x130U, seed_right);

    put_instruction(system.rsp, 0x00U, addiu(1, 0, 0x100));
    put_instruction(system.rsp, 0x04U, addiu(2, 0, 0x200));
    put_instruction(system.rsp, 0x08U, vector_load(1, 0, 1));
    put_instruction(system.rsp, 0x0cU, vector_load(2, 1, 1));
    put_instruction(system.rsp, 0x10U, vector_load(5, 2, 1));
    put_instruction(system.rsp, 0x14U, vector_load(6, 3, 1));
    put_instruction(system.rsp, 0x18U, vector_op(0x05U, 7, 5, 6, 0));
    put_instruction(system.rsp, 0x1cU, vector_op(function, destination, 1, 2, element));
    put_instruction(system.rsp, 0x20U, vector_store(destination, 0, 2));
    put_instruction(system.rsp, 0x24U, vector_op(0x1dU, 4, 0, 0, 8));
    put_instruction(system.rsp, 0x28U, vector_store(4, 1, 2));
    put_instruction(system.rsp, 0x2cU, vector_op(0x1dU, 5, 0, 0, 9));
    put_instruction(system.rsp, 0x30U, vector_store(5, 2, 2));
    put_instruction(system.rsp, 0x34U, vector_op(0x1dU, 6, 0, 0, 10));
    put_instruction(system.rsp, 0x38U, vector_store(6, 3, 2));
    put_instruction(system.rsp, 0x3cU, 0x0000000dU);

    system.rsp.pc = 0;
    system.rsp.write_register(0x10U, 1U);
    system.rsp.tick(1024);
    CHECK((system.rsp.read_register(0x10U) & 1U) != 0);

    return {read_vector(system.rsp, 0x200U), read_vector(system.rsp, 0x210U), read_vector(system.rsp, 0x220U),
            read_vector(system.rsp, 0x230U)};
}

constexpr std::array<u16, 8> left_values{
    0x8000U, 0x8000U, 0x7fffU, 0x7fffU, 0xffffU, 0x0001U, 0x4000U, 0xc000U,
};
constexpr std::array<u16, 8> right_values{
    0x8000U, 0x7fffU, 0x8000U, 0x7fffU, 0xffffU, 0x0001U, 0xc000U, 0x4000U,
};
constexpr std::array<u16, 8> seed_left_values{
    0x7fffU, 0x8000U, 0x7fffU, 0x8000U, 0x4000U, 0xc000U, 0x0001U, 0xffffU,
};
constexpr std::array<u16, 8> seed_right_values{
    0x7fffU, 0x8000U, 0x8000U, 0x7fffU, 0x4000U, 0x4000U, 0xffffU, 0x0001U,
};

s64 expected_accumulator(unsigned function, s64 seed, s64 product) {
    switch (function) {
    case 0x00U:
    case 0x01U:
        return wrap_accumulator(product * 2 + 0x8000);
    case 0x07U:
        return wrap_accumulator(product * 0x1'0000);
    case 0x08U:
    case 0x09U:
        return wrap_accumulator(seed + product * 2);
    case 0x0fU: {
        const s64 upper = seed >> 16U;
        const s64 result = upper + product;
        const u64 bits =
            (static_cast<u64>(seed) & 0xffffU) | ((static_cast<u64>(result) & 0xffff'ffffU) << 16U);
        return wrap_accumulator(static_cast<s64>(bits));
    }
    default:
        return seed;
    }
}

u16 expected_result(unsigned function, s64 accumulator) {
    if (function == 0x01U)
        return unsigned_vmulf_result(accumulator);
    if (function == 0x09U)
        return unsigned_vmacf_result(accumulator);
    return signed_saturation(accumulator);
}

} // namespace

TEST(rsp_signed_multiply_slice_preserves_extrema_selectors_aliases_and_accumulator_carries) {
    for (const unsigned function : {0x00U, 0x01U, 0x07U, 0x08U, 0x09U, 0x0fU}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                const auto observed = execute(function, element, destination, left_values, right_values,
                                              seed_left_values, seed_right_values);
                for (unsigned lane = 0; lane < 8; ++lane) {
                    const s64 seed_product = static_cast<s64>(std::bit_cast<s16>(seed_left_values[lane])) *
                                             seed_right_values[lane];
                    const s64 seed = wrap_accumulator(seed_product);
                    const s64 product = static_cast<s64>(std::bit_cast<s16>(left_values[lane])) *
                                        std::bit_cast<s16>(right_values[selected_lane(element, lane)]);
                    const s64 accumulator = expected_accumulator(function, seed, product);
                    CHECK_EQ(observed.result[lane], expected_result(function, accumulator));
                    CHECK_EQ(observed.high[lane], acc_high(accumulator));
                    CHECK_EQ(observed.mid[lane], acc_mid(accumulator));
                    CHECK_EQ(observed.low[lane], acc_low(accumulator));
                }
            }
        }
    }
}
