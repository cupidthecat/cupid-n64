#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <bit>
#include <limits>

using namespace cupid;

namespace {

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

struct Observation {
    std::array<u16, 8> result{};
    std::array<u16, 8> acc_high{};
    std::array<u16, 8> acc_mid{};
    std::array<u16, 8> acc_low{};
    u16 vco = 0;
};

Observation execute(unsigned function, unsigned element, unsigned destination, const std::array<u16, 8>& left,
                    const std::array<u16, 8>& right) {
    System system;
    write_vector(system.rsp, 0x100, left);
    write_vector(system.rsp, 0x110, right);

    put_instruction(system.rsp, 0x00, addiu(1, 0, 0x100));
    put_instruction(system.rsp, 0x04, addiu(2, 0, 0x200));
    put_instruction(system.rsp, 0x08, vector_load(1, 0, 1));
    put_instruction(system.rsp, 0x0c, vector_load(2, 1, 1));
    put_instruction(system.rsp, 0x10, vector_op(function, destination, 1, 2, element));
    put_instruction(system.rsp, 0x14, vector_store(destination, 0, 2));
    put_instruction(system.rsp, 0x18, vector_op(0x1d, 4, 0, 0, 8));
    put_instruction(system.rsp, 0x1c, vector_store(4, 1, 2));
    put_instruction(system.rsp, 0x20, vector_op(0x1d, 5, 0, 0, 9));
    put_instruction(system.rsp, 0x24, vector_store(5, 2, 2));
    put_instruction(system.rsp, 0x28, vector_op(0x1d, 6, 0, 0, 10));
    put_instruction(system.rsp, 0x2c, vector_store(6, 3, 2));
    put_instruction(system.rsp, 0x30, cfc2(3, 0));
    put_instruction(system.rsp, 0x34, sw(3, 2, 0x40));
    put_instruction(system.rsp, 0x38, break_instruction());

    system.rsp.pc = 0;
    system.rsp.write_register(0x10, 1U << 0);
    system.rsp.tick(512);
    CHECK((system.rsp.read_register(0x10) & 1U) != 0);

    return {read_vector(system.rsp, 0x200), read_vector(system.rsp, 0x210), read_vector(system.rsp, 0x220),
            read_vector(system.rsp, 0x230), static_cast<u16>(read_word(system.rsp, 0x240))};
}

constexpr std::array<u16, 8> left_values{
    0x7fff, 0x8000, 0xffff, 0x0001, 0x4000, 0xc000, 0x1234, 0xfedc,
};
constexpr std::array<u16, 8> right_values{
    0x0001, 0xffff, 0x8000, 0x7fff, 0x4001, 0xbfff, 0xedcb, 0x0124,
};

} // namespace

TEST(rsp_vector_preselected_elements_preserve_logic_and_aliasing) {
    for (const unsigned function : {0x28U, 0x2cU}) {
        for (unsigned element = 0; element < 16; ++element) {
            for (const unsigned destination : {3U, 1U, 2U}) {
                const auto observed = execute(function, element, destination, left_values, right_values);
                for (unsigned lane = 0; lane < 8; ++lane) {
                    const u16 right = right_values[selected_lane(element, lane)];
                    const u16 expected = function == 0x28U ? static_cast<u16>(left_values[lane] & right)
                                                           : static_cast<u16>(left_values[lane] ^ right);
                    CHECK_EQ(observed.result[lane], expected);
                    CHECK_EQ(observed.acc_high[lane], 0U);
                    CHECK_EQ(observed.acc_mid[lane], 0U);
                    CHECK_EQ(observed.acc_low[lane], expected);
                }
                CHECK_EQ(observed.vco, 0U);
            }
        }
    }
}

TEST(rsp_vector_preselected_elements_preserve_vaddc_carries) {
    for (unsigned element = 0; element < 16; ++element) {
        for (const unsigned destination : {3U, 1U, 2U}) {
            const auto observed = execute(0x14, element, destination, left_values, right_values);
            u16 carries = 0;
            for (unsigned lane = 0; lane < 8; ++lane) {
                const u16 right = right_values[selected_lane(element, lane)];
                const u32 sum = static_cast<u32>(left_values[lane]) + right;
                const u16 expected = static_cast<u16>(sum);
                if (sum > 0xffffU)
                    carries |= static_cast<u16>(1U << lane);
                CHECK_EQ(observed.result[lane], expected);
                CHECK_EQ(observed.acc_high[lane], 0U);
                CHECK_EQ(observed.acc_mid[lane], 0U);
                CHECK_EQ(observed.acc_low[lane], expected);
            }
            CHECK_EQ(observed.vco, carries);
        }
    }
}

TEST(rsp_vector_preselected_elements_preserve_vmulf_accumulator) {
    for (unsigned element = 0; element < 16; ++element) {
        for (const unsigned destination : {3U, 1U, 2U}) {
            const auto observed = execute(0x00, element, destination, left_values, right_values);
            for (unsigned lane = 0; lane < 8; ++lane) {
                const s64 left = std::bit_cast<s16>(left_values[lane]);
                const s64 right = std::bit_cast<s16>(right_values[selected_lane(element, lane)]);
                const s64 accumulator = left * right * 2 + 0x8000;
                const u64 bits = static_cast<u64>(accumulator) & ((u64{1} << 48U) - 1U);
                const s64 shifted = accumulator >> 16U;
                const s64 clamped =
                    shifted > std::numeric_limits<s16>::max()   ? std::numeric_limits<s16>::max()
                    : shifted < std::numeric_limits<s16>::min() ? std::numeric_limits<s16>::min()
                                                                : shifted;
                CHECK_EQ(observed.result[lane], std::bit_cast<u16>(static_cast<s16>(clamped)));
                CHECK_EQ(observed.acc_high[lane], static_cast<u16>(bits >> 32U));
                CHECK_EQ(observed.acc_mid[lane], static_cast<u16>(bits >> 16U));
                CHECK_EQ(observed.acc_low[lane], static_cast<u16>(bits));
            }
            CHECK_EQ(observed.vco, 0U);
        }
    }
}
