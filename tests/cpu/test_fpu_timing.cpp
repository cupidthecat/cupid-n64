#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <bit>
#include <limits>

namespace {
using namespace cupid;

struct Measurement {
    u64 cycles;
    u64 result;
    u32 control;
};

constexpr u64 unchanged_result = 0x123456789abcdef0ULL;

Measurement run(unsigned format, unsigned function, u64 left, u64 right, u32 control = 1U << 24,
                bool trap = false) {
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x34000000);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.fpu.control = control;
    cpu.fpu.registers[2] = left;
    cpu.fpu.registers[4] = right;
    cpu.fpu.registers[6] = unchanged_result;
    const u32 instruction = 0x44000000U | (format << 21U) | (4U << 16U) | (2U << 11U) | (6U << 6U) | function;
    system.bus.write(0x1000, 4, instruction);
    u64 ignored = 0;
    CHECK(cpu.read_memory(cpu.pc, 4, ignored, true));
    cpu.step();
    CHECK_EQ(cpu.exception_pending, trap);
    return {cpu.cycles, cpu.fpu.registers[6], cpu.fpu.control};
}

template <class T> void arithmetic(unsigned function, T left, T right, T expected, u64 cycles) {
    using Bits = std::conditional_t<sizeof(T) == 4, u32, u64>;
    const auto result =
        run(sizeof(T) == 4 ? 0x10U : 0x11U, function, std::bit_cast<Bits>(left), std::bit_cast<Bits>(right));
    CHECK_EQ(result.cycles, cycles);
    CHECK_EQ(result.result, std::bit_cast<Bits>(expected));
}
} // namespace

TEST(fpu_special_operands_shortcut_arithmetic_latency) {
    arithmetic<float>(0, 0.0f, 2.5f, 2.5f, 2);
    arithmetic<double>(1, 2.5, 0.0, 2.5, 2);
    arithmetic<float>(2, 2.5f, 2.0f, 5.0f, 2);
    arithmetic<double>(2, 2.0, 2.5, 5.0, 2);
    arithmetic<float>(3, 0.0f, 2.5f, 0.0f, 2);
    arithmetic<double>(3, 2.5, std::numeric_limits<double>::infinity(), 0.0, 2);
    arithmetic<float>(4, 0.0f, 0.0f, 0.0f, 2);
    arithmetic<double>(4, std::numeric_limits<double>::infinity(), 0.0,
                       std::numeric_limits<double>::infinity(), 2);
}

TEST(fpu_normal_arithmetic_retains_full_latency) {
    arithmetic<float>(0, 2.5f, 3.5f, 6.0f, 3);
    arithmetic<double>(1, 3.5, 2.5, 1.0, 3);
    arithmetic<float>(2, 2.5f, 3.5f, 8.75f, 5);
    arithmetic<double>(2, 2.5, 3.5, 8.75, 8);
    arithmetic<float>(3, 2.5f, 2.5f, 1.0f, 29);
    arithmetic<double>(3, 2.5, 2.5, 1.0, 58);
    arithmetic<float>(4, 4.0f, 0.0f, 2.0f, 29);
    arithmetic<double>(4, 4.0, 0.0, 2.0, 58);
}

TEST(fpu_zero_integer_conversion_shortcuts_the_normalization_stage) {
    const auto word_zero = run(0x14, 0x20, 0x1234567800000000ULL, 0);
    CHECK_EQ(word_zero.cycles, 2U);
    CHECK_EQ(word_zero.result, 0U);
    for (unsigned format : {0x14U, 0x15U}) {
        for (unsigned function : {0x20U, 0x21U}) {
            const auto zero = run(format, function, 0, 0);
            CHECK_EQ(zero.cycles, 2U);
            CHECK_EQ(zero.result, 0U);
            const auto one = run(format, function, 1, 0);
            CHECK_EQ(one.cycles, 5U);
            CHECK_EQ(one.result, function == 0x20U ? 0x3f800000ULL : 0x3ff0000000000000ULL);
        }
    }
}

TEST(fpu_arithmetic_interlock_checks_both_sources_including_register_zero) {
    for (unsigned destination : {0U, 6U}) {
        for (unsigned source : {0U, 1U, 2U}) {
            System system;
            test::initialize_memory(system);
            auto& cpu = system.cpu;
            cpu.write_cop0(12, 0x34000000);
            cpu.set_pc(0xffffffff80001000ULL);
            cpu.fpu.registers[2] = std::bit_cast<u32>(2.5f);
            cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
            const unsigned fs = source == 0 ? destination : 2;
            const unsigned ft = source == 1 ? destination : 4;
            system.bus.write(0x1000, 4, 0x46041000U | (destination << 6));
            system.bus.write(0x1004, 4, 0x46000000U | (ft << 16) | (fs << 11) | (destination << 6));
            u64 ignored = 0;
            CHECK(cpu.read_memory(cpu.pc, 4, ignored, true));
            cpu.step();
            cpu.step();
            CHECK_EQ(cpu.cycles, source == 2 ? 6U : 7U);
            const float expected = source == 0 ? 9.5f : source == 1 ? 8.5f : 6.0f;
            CHECK_EQ(cpu.fpu.registers[destination], std::bit_cast<u32>(expected));
        }
    }
}

TEST(fpu_arithmetic_source_exceptions_use_the_early_completion_path) {
    for (unsigned format : {0x10U, 0x11U}) {
        for (unsigned function = 0; function <= 4; ++function) {
            const u64 one = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
            const auto result = run(format, function, 1, one, 0, true);
            CHECK_EQ(result.cycles, 6U);
            CHECK_EQ(result.result, unchanged_result);
            CHECK_EQ(result.control & 0x20000U, 0x20000U);
        }
    }
}

TEST(fpu_arithmetic_result_exceptions_include_the_operation_latency) {
    for (unsigned format : {0x10U, 0x11U}) {
        const bool single = format == 0x10U;
        const u64 largest = single ? 0x7f7fffffULL : 0x7fefffffffffffffULL;
        const u64 sign = single ? 0x80000000ULL : 0x8000000000000000ULL;
        const u64 one_and_half = single ? 0x3fc00000ULL : 0x3ff8000000000000ULL;
        const u64 one_eighth = single ? 0x3e000000ULL : 0x3fc0000000000000ULL;
        for (unsigned function = 0; function <= 4; ++function) {
            const u64 right = function == 0   ? largest
                              : function == 1 ? largest | sign
                              : function == 2 ? one_and_half
                                              : one_eighth;
            const auto result = run(format, function, largest, right, 0xf80, true);
            const u64 latency = function < 2 ? 3U : function == 2 ? (single ? 5U : 8U) : (single ? 29U : 58U);
            CHECK_EQ(result.cycles, latency + 4);
            CHECK_EQ(result.result, unchanged_result);
            CHECK((result.control & 0x1f000U) != 0);
        }
    }
}

TEST(fpu_multiply_underflow_only_shortcuts_when_it_can_flush) {
    for (unsigned format : {0x10U, 0x11U}) {
        const bool single = format == 0x10U;
        const u64 left = single ? 0x00800001ULL : 0x0010000000000001ULL;
        const u64 right = single ? std::bit_cast<u32>(0.13f) : std::bit_cast<u64>(0.13);
        for (const u32 control : {0U, 0x01000080U, 0x01000100U, 0x01000180U}) {
            const auto trapped = run(format, 2, left, right, control, true);
            CHECK_EQ(trapped.cycles, single ? 9U : 12U);
            CHECK_EQ(trapped.result, unchanged_result);
            CHECK_EQ(trapped.control & 0x20000U, 0x20000U);
        }
        const auto flushed = run(format, 2, left, right);
        CHECK_EQ(flushed.cycles, 2U);
        CHECK_EQ(flushed.result, 0U);
    }
}

TEST(fpu_arithmetic_special_result_exceptions_finish_early) {
    for (unsigned format : {0x10U, 0x11U}) {
        const u64 one = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
        const u64 negative_one = one | (format == 0x10U ? 0x80000000ULL : 0x8000000000000000ULL);
        for (unsigned function : {3U, 4U}) {
            const auto result = run(format, function, function == 3 ? one : negative_one, 0, 0xf80, true);
            CHECK_EQ(result.cycles, 6U);
            CHECK_EQ(result.result, unchanged_result);
        }
    }
}

TEST(fpu_integer_conversion_source_exceptions_finish_early) {
    for (unsigned format : {0x10U, 0x11U}) {
        for (unsigned function : {8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 0x24U, 0x25U}) {
            const auto result = run(format, function, 1, 0, 0, true);
            CHECK_EQ(result.cycles, 6U);
            CHECK_EQ(result.result, unchanged_result);
            CHECK_EQ(result.control & 0x20000U, 0x20000U);
        }
    }
}

TEST(fpu_integer_conversion_inexact_traps_wait_for_rounding) {
    for (unsigned format : {0x10U, 0x11U}) {
        for (unsigned function : {8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 0x24U, 0x25U}) {
            const u64 half = format == 0x10U ? 0x3f000000ULL : 0x3fe0000000000000ULL;
            const auto result = run(format, function, half, 0, 0x80, true);
            CHECK_EQ(result.cycles, 9U);
            CHECK_EQ(result.result, unchanged_result);
            CHECK_EQ(result.control & 0x1000U, 0x1000U);
        }
    }
}

TEST(fpu_integer_conversion_range_faults_distinguish_exponent_and_result_checks) {
    for (unsigned format : {0x10U, 0x11U}) {
        for (unsigned function : {8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 0x24U, 0x25U}) {
            const bool long_result = function < 12 || function == 0x25;
            const double boundary = long_result ? 0x1p53 : 0x1p32;
            for (const double value : {boundary, -boundary}) {
                const u64 bits = format == 0x10U ? std::bit_cast<u32>(static_cast<float>(value))
                                                 : std::bit_cast<u64>(value);
                const auto result = run(format, function, bits, 0, 0, true);
                CHECK_EQ(result.cycles, 6U);
                CHECK_EQ(result.result, unchanged_result);
            }
            if (!long_result) {
                const u64 bits = format == 0x10U ? std::bit_cast<u32>(0x1p31f) : std::bit_cast<u64>(0x1p31);
                const auto result = run(format, function, bits, 0, 0, true);
                CHECK_EQ(result.cycles, 9U);
                CHECK_EQ(result.result, unchanged_result);
            }
        }
    }
}

TEST(fpu_integer_to_float_traps_include_normalization_latency) {
    for (unsigned format : {0x14U, 0x15U}) {
        const auto inexact = run(format, 0x20, 1234567891, 0, 0x80, true);
        CHECK_EQ(inexact.cycles, 9U);
        CHECK_EQ(inexact.result, unchanged_result);
        for (unsigned function : {0U, 0x0cU, 0x24U}) {
            const auto unimplemented = run(format, function, 0, 0, 0, true);
            CHECK_EQ(unimplemented.cycles, 6U);
            CHECK_EQ(unimplemented.result, unchanged_result);
        }
    }
    for (unsigned function : {0x20U, 0x21U}) {
        const auto range = run(0x15, function, 1ULL << 55, 0, 0, true);
        CHECK_EQ(range.cycles, 6U);
        CHECK_EQ(range.result, unchanged_result);
    }
    const auto inexact = run(0x15, 0x21, (1ULL << 55) - 3, 0, 0x80, true);
    CHECK_EQ(inexact.cycles, 9U);
    CHECK_EQ(inexact.result, unchanged_result);
}

TEST(fpu_double_to_single_traps_keep_the_conversion_latency) {
    for (const u64 bits : {1ULL, 0x7fefffffffffffffULL, 0x7ff8000000000000ULL}) {
        const auto result = run(0x11, 0x20, bits, 0, 0xf80, true);
        CHECK_EQ(result.cycles, 6U);
        CHECK_EQ(result.result, unchanged_result);
    }
}

TEST(fpu_reserved_opcodes_fault_before_integer_format_checks) {
    for (unsigned format : {0x10U, 0x11U, 0x14U, 0x15U}) {
        for (unsigned function : {0x10U, 0x1fU, 0x22U, 0x23U, 0x26U, 0x2fU}) {
            const auto result = run(format, function, 0, 0, 0, true);
            CHECK_EQ(result.cycles, 5U);
            CHECK_EQ(result.result, unchanged_result);
            CHECK_EQ(result.control & 0x20000U, 0x20000U);
        }
    }
}
