#include "cupid/system.hpp"
#include "test.hpp"

#include "../../src/rsp/divider_tables.hpp"

#include <array>
#include <bit>

using namespace cupid;

namespace {

constexpr u32 break_instruction = 0x0000000dU;
constexpr u32 input_address = 0x100U;
constexpr u32 output_address = 0x200U;

constexpr u32 addiu(unsigned rt, unsigned rs, s16 immediate) {
    return (0x09U << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(immediate);
}

constexpr u32 vector_op(unsigned function, unsigned vd, unsigned de, unsigned vt, unsigned element) {
    return (0x12U << 26) | ((0x10U | (element & 15U)) << 21) | (vt << 16) | ((de & 7U) << 11) | (vd << 6) |
           (function & 63U);
}

constexpr u32 vector_load(unsigned vt, s8 immediate, unsigned base) {
    return (0x32U << 26) | (base << 21) | (vt << 16) | (0x04U << 11) | (static_cast<u8>(immediate) & 0x7fU);
}

constexpr u32 vector_store(unsigned vt, s8 immediate, unsigned base) {
    return (0x3aU << 26) | (base << 21) | (vt << 16) | (0x04U << 11) | (static_cast<u8>(immediate) & 0x7fU);
}

void put_instruction(Rsp& rsp, u32 address, u32 instruction) {
    write_be32(&rsp.memory[0x1000U | (address & 0xffcU)], instruction);
}

void write_lane(Rsp& rsp, u32 address, unsigned lane, u16 value) {
    const u32 offset = (address + lane * 2U) & 0xfffU;
    rsp.memory[offset] = static_cast<u8>(value >> 8U);
    rsp.memory[(offset + 1U) & 0xfffU] = static_cast<u8>(value);
}

u16 read_lane(const Rsp& rsp, u32 address, unsigned lane) {
    const u32 offset = (address + lane * 2U) & 0xfffU;
    return static_cast<u16>((static_cast<u16>(rsp.memory[offset]) << 8U) |
                            rsp.memory[(offset + 1U) & 0xfffU]);
}

void clear_vector(Rsp& rsp, u32 address) {
    for (unsigned byte = 0; byte < 16U; ++byte)
        rsp.memory[(address + byte) & 0xfffU] = 0;
}

void install_full_divider_program(Rsp& rsp, unsigned high_function, unsigned low_function) {
    put_instruction(rsp, 0x00U, addiu(1, 0, static_cast<s16>(input_address)));
    put_instruction(rsp, 0x04U, addiu(2, 0, static_cast<s16>(output_address)));
    put_instruction(rsp, 0x08U, vector_load(1, 0, 1));
    put_instruction(rsp, 0x0cU, vector_op(high_function, 2, 0, 1, 8));
    put_instruction(rsp, 0x10U, vector_op(low_function, 2, 1, 1, 9));
    put_instruction(rsp, 0x14U, vector_op(high_function, 2, 2, 1, 8));
    put_instruction(rsp, 0x18U, vector_store(2, 0, 2));
    put_instruction(rsp, 0x1cU, break_instruction);
}

u32 execute_full_divider(System& system, u32 input) {
    clear_vector(system.rsp, input_address);
    clear_vector(system.rsp, output_address);
    write_lane(system.rsp, input_address, 0, static_cast<u16>(input >> 16U));
    write_lane(system.rsp, input_address, 1, static_cast<u16>(input));
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10U, 0x05U); // Clear HALT/BROKE and run to BREAK.
    system.rsp.tick(128);
    CHECK_EQ(system.rsp.read_register(0x10U) & 3U, 3U);
    return (static_cast<u32>(read_lane(system.rsp, output_address, 2)) << 16U) |
           read_lane(system.rsp, output_address, 1);
}

u16 reciprocal_entry_oracle(unsigned index) {
    if (index == 0)
        return 0xffffU;
    const u64 quotient = (u64{1} << 34) / (index + 512U);
    return static_cast<u16>((quotient + 1U) >> 8U);
}

u16 reciprocal_sqrt_entry_oracle(unsigned index) {
    const u64 a = index < 256U ? index + 256U : 2U * (index - 256U) + 512U;
    u64 b = u64{1} << 17;
    for (u64 increment = 512; increment != 0; increment >>= 1U) {
        while (a * (b + increment) * (b + increment) < (u64{1} << 44))
            b += increment;
    }
    return static_cast<u16>(b >> 1U);
}

u32 divider_oracle(u32 value, bool square_root) {
    if (value == 0)
        return 0x7fff'ffffU;
    if (value == 0xffff'8000U)
        return 0xffff'0000U;

    const u32 adjusted = value > 0xffff'8000U ? value - 1U : value;
    const bool negative = std::bit_cast<s32>(adjusted) < 0;
    const u32 positive = negative ? ~adjusted : adjusted;
    const unsigned shift = static_cast<unsigned>(std::countl_zero(positive)) + 1U;
    const u32 normalized = positive << (shift & 31U);
    u16 table = 0;
    unsigned output_shift = 0;
    if (square_root) {
        const unsigned index = (normalized >> 24U) | ((shift & 1U) << 8U);
        table = reciprocal_sqrt_entry_oracle(index);
        output_shift = (32U - shift) >> 1U;
    } else {
        table = reciprocal_entry_oracle(normalized >> 23U);
        output_shift = 32U - shift;
    }
    const u32 magnitude = (0x4000'0000U | (static_cast<u32>(table) << 14U)) >> output_shift;
    return negative ? ~magnitude : magnitude;
}

u32 reciprocal_input(unsigned index, unsigned shift) {
    CHECK(shift >= 2U);
    CHECK(shift <= 23U);
    return (u32{1} << (32U - shift)) | (static_cast<u32>(index) << (23U - shift));
}

u32 reciprocal_sqrt_input(unsigned index, unsigned shift) {
    CHECK(shift >= 2U);
    CHECK(shift <= 24U);
    CHECK_EQ(shift & 1U, index >> 8U);
    return (u32{1} << (32U - shift)) | ((static_cast<u32>(index) & 0xffU) << (24U - shift));
}

unsigned reciprocal_index(u32 value) {
    const unsigned shift = static_cast<unsigned>(std::countl_zero(value)) + 1U;
    return (value << (shift & 31U)) >> 23U;
}

unsigned reciprocal_sqrt_index(u32 value) {
    const unsigned shift = static_cast<unsigned>(std::countl_zero(value)) + 1U;
    return ((value << (shift & 31U)) >> 24U) | ((shift & 1U) << 8U);
}

void install_latch_clear_program(Rsp& rsp, unsigned high_function, unsigned low_function,
                                 unsigned direct_function) {
    put_instruction(rsp, 0x00U, addiu(1, 0, static_cast<s16>(input_address)));
    put_instruction(rsp, 0x04U, addiu(2, 0, static_cast<s16>(output_address)));
    put_instruction(rsp, 0x08U, vector_load(1, 0, 1));
    put_instruction(rsp, 0x0cU, vector_op(high_function, 2, 0, 1, 8));
    put_instruction(rsp, 0x10U, vector_op(low_function, 2, 1, 1, 9));
    put_instruction(rsp, 0x14U, vector_op(low_function, 2, 2, 1, 10));
    put_instruction(rsp, 0x18U, vector_op(high_function, 2, 3, 1, 8));
    put_instruction(rsp, 0x1cU, vector_op(direct_function, 2, 5, 1, 10));
    put_instruction(rsp, 0x20U, vector_op(low_function, 2, 6, 1, 11));
    put_instruction(rsp, 0x24U, vector_op(high_function, 2, 7, 1, 8));
    put_instruction(rsp, 0x28U, vector_store(2, 0, 2));
    put_instruction(rsp, 0x2cU, break_instruction);
}

void check_latch_clear(System& system, bool square_root) {
    const u16 high = 0x1234U;
    const u16 combined_low = 0x5678U;
    const u16 after_low = 0x8001U;
    const u16 after_direct = 0x7fffU;
    clear_vector(system.rsp, input_address);
    clear_vector(system.rsp, output_address);
    write_lane(system.rsp, input_address, 0, high);
    write_lane(system.rsp, input_address, 1, combined_low);
    write_lane(system.rsp, input_address, 2, after_low);
    write_lane(system.rsp, input_address, 3, after_direct);
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10U, 0x05U);
    system.rsp.tick(128);
    CHECK_EQ(system.rsp.read_register(0x10U) & 3U, 3U);

    const u32 low_cleared = (static_cast<u32>(read_lane(system.rsp, output_address, 3)) << 16U) |
                            read_lane(system.rsp, output_address, 2);
    CHECK_EQ(low_cleared,
             divider_oracle(static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(after_low))), square_root));

    const u32 direct_cleared = (static_cast<u32>(read_lane(system.rsp, output_address, 7)) << 16U) |
                               read_lane(system.rsp, output_address, 6);
    CHECK_EQ(
        direct_cleared,
        divider_oracle(static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(after_direct))), square_root));
}

} // namespace

TEST(rsp_divider_constexpr_tables_match_independent_runtime_oracles) {
    for (unsigned index = 0; index < 512U; ++index) {
        CHECK_EQ(rsp::reciprocal_table[index], reciprocal_entry_oracle(index));
        CHECK_EQ(rsp::reciprocal_sqrt_table[index], reciprocal_sqrt_entry_oracle(index));
    }
}

TEST(rsp_reciprocal_table_indices_execute_through_vrcph_vrcpl_vrcph) {
    System system;
    install_full_divider_program(system.rsp, 0x32U, 0x31U);
    for (unsigned index = 0; index < 512U; ++index) {
        for (const unsigned shift : {2U, 3U}) {
            const u32 input = reciprocal_input(index, shift);
            CHECK_EQ(reciprocal_index(input), index);
            CHECK_EQ(execute_full_divider(system, input), divider_oracle(input, false));
        }
    }

    constexpr std::array<u32, 8> edges{
        0x0000'0000U, 0x0000'0001U, 0x0000'7fffU, 0xffff'8000U,
        0xffff'8001U, 0x7fff'ffffU, 0x8000'0000U, 0xffff'ffffU,
    };
    for (const u32 input : edges)
        CHECK_EQ(execute_full_divider(system, input), divider_oracle(input, false));
}

TEST(rsp_reciprocal_sqrt_table_indices_execute_through_vrsqh_vrsql_vrsqh) {
    System system;
    install_full_divider_program(system.rsp, 0x36U, 0x35U);
    for (unsigned index = 0; index < 512U; ++index) {
        const unsigned first_shift = index < 256U ? 2U : 3U;
        for (const unsigned shift : {first_shift, first_shift + 2U}) {
            const u32 input = reciprocal_sqrt_input(index, shift);
            CHECK_EQ(reciprocal_sqrt_index(input), index);
            CHECK_EQ(execute_full_divider(system, input), divider_oracle(input, true));
        }
    }

    constexpr std::array<u32, 8> edges{
        0x0000'0000U, 0x0000'0001U, 0x0000'7fffU, 0xffff'8000U,
        0xffff'8001U, 0x7fff'ffffU, 0x8000'0000U, 0xffff'ffffU,
    };
    for (const u32 input : edges)
        CHECK_EQ(execute_full_divider(system, input), divider_oracle(input, true));
}

TEST(rsp_divider_low_and_direct_operations_clear_the_high_input_latch) {
    System reciprocal_system;
    install_latch_clear_program(reciprocal_system.rsp, 0x32U, 0x31U, 0x30U);
    check_latch_clear(reciprocal_system, false);

    System sqrt_system;
    install_latch_clear_program(sqrt_system.rsp, 0x36U, 0x35U, 0x34U);
    check_latch_clear(sqrt_system, true);
}
