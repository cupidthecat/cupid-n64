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
};

Measurement run(unsigned format, unsigned function, u64 left, u64 right) {
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x34000000);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.fpu.control = 1U << 24;
    cpu.fpu.registers[2] = left;
    cpu.fpu.registers[4] = right;
    const u32 instruction = 0x44000000U | (format << 21U) | (4U << 16U) | (2U << 11U) | (6U << 6U) | function;
    system.bus.write(0x1000, 4, instruction);
    u64 ignored = 0;
    CHECK(cpu.read_memory(cpu.pc, 4, ignored, true));
    cpu.step();
    CHECK(!cpu.exception_pending);
    return {cpu.cycles, cpu.fpu.registers[6]};
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
