#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <limits>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr unsigned fs = 3;
constexpr unsigned ft = 5;
constexpr unsigned fd = 7;

constexpr u32 arithmetic_instruction(unsigned format, unsigned function) {
    return (0x11U << 26U) | (format << 21U) | (ft << 16U) | (fs << 11U) | (fd << 6U) | function;
}

void prepare(System& system, bool full_registers, u32 control, u32 instruction) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x20000000U | (full_registers ? 0x04000000U : 0U));
    system.cpu.set_pc(code);
    system.cpu.fpu.control = control;
    system.bus.write(0x1000, 4, instruction);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code, 4, ignored, true));
}

void set_operands(Fpu& fpu, bool full_registers, unsigned format, u64 left, u64 right) {
    if (format == 0x10U) {
        const u32 left_word = static_cast<u32>(left);
        const u32 right_word = static_cast<u32>(right);
        if (full_registers) {
            fpu.registers[fs] = (0xabcddcbaULL << 32U) | left_word;
        } else {
            // Arithmetic fs uses the even register's low word, unlike an odd
            // MTC1/MFC1 transfer. The encoded ft still selects its own register.
            auto& pair = fpu.registers[fs & ~1U];
            pair = (pair & 0xffffffff00000000ULL) | left_word;
            fpu.registers[fs] = 0x7fc00000U;
        }
        fpu.registers[ft] = (0x12344321ULL << 32U) | right_word;
        return;
    }

    fpu.registers[full_registers ? fs : (fs & ~1U)] = left;
    fpu.registers[ft] = right;
}

unsigned preflight(unsigned format, unsigned function, u64 left, u64 right, u32 control = 1U << 24U,
                   bool full_registers = true) {
    System system;
    const u32 instruction = arithmetic_instruction(format, function);
    prepare(system, full_registers, control, instruction);
    set_operands(system.cpu.fpu, full_registers, format, left, right);
    return system.cpu.fpu.nontrapping_arithmetic_cycles(instruction);
}

void cost_matches_step(unsigned format, unsigned function, u64 left, u64 right, u32 control,
                       bool full_registers) {
    System system;
    const u32 instruction = arithmetic_instruction(format, function);
    prepare(system, full_registers, control, instruction);
    set_operands(system.cpu.fpu, full_registers, format, left, right);
    const unsigned expected = system.cpu.fpu.nontrapping_arithmetic_cycles(instruction);
    CHECK(expected != 0U);
    system.cpu.step();
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.cycles, expected);
}

} // namespace

TEST(fpu_nontrapping_arithmetic_preflight_matches_step_cycles_across_rounding_fr_and_formats) {
    for (const bool full_registers : {false, true}) {
        for (u32 rounding = 0; rounding < 4U; ++rounding) {
            const u32 control = (1U << 24U) | rounding;
            for (unsigned function = 0; function < 4U; ++function) {
                cost_matches_step(0x10U, function, std::bit_cast<u32>(2.5f), std::bit_cast<u32>(3.5f),
                                  control, full_registers);
                cost_matches_step(0x11U, function, std::bit_cast<u64>(2.5), std::bit_cast<u64>(3.5), control,
                                  full_registers);
            }
        }
    }

    cost_matches_step(0x10U, 0, std::bit_cast<u32>(0.0f), std::bit_cast<u32>(2.5f), 1U << 24U, true);
    cost_matches_step(0x11U, 3, std::bit_cast<u64>(2.5),
                      std::bit_cast<u64>(std::numeric_limits<double>::infinity()), 1U << 24U, true);
    cost_matches_step(0x10U, 3, std::bit_cast<u32>(1.0f), std::bit_cast<u32>(0.0f), 1U << 24U, true);
    cost_matches_step(0x10U, 0, std::bit_cast<u32>(2.5f), std::bit_cast<u32>(3.5f), (1U << 24U) | (1U << 11U),
                      true);
    cost_matches_step(0x11U, 3, std::bit_cast<u64>(1.0), std::bit_cast<u64>(0.0), (1U << 24U) | (1U << 11U),
                      true);

    // A power-of-two operand keeps MUL on its one-cycle arithmetic shortcut even
    // when the product flushes below the normal range.
    cost_matches_step(0x10U, 2, 0x00800001U, std::bit_cast<u32>(0.5f), 1U << 24U, true);
    cost_matches_step(0x11U, 2, 0x0010000000000001ULL, std::bit_cast<u64>(0.5), 1U << 24U, true);
}

TEST(fpu_nontrapping_arithmetic_preflight_rejects_only_the_bounded_fault_and_variable_latency_domain) {
    constexpr u32 fs_mode = 1U << 24U;
    const u32 one_s = std::bit_cast<u32>(1.0f);
    const u64 one_d = std::bit_cast<u64>(1.0);

    CHECK_EQ(preflight(0x10U, 0, one_s, one_s, 0), 0U);
    for (unsigned bit = 7; bit <= 10; ++bit)
        CHECK_EQ(preflight(0x10U, 0, one_s, one_s, fs_mode | (1U << bit)), 0U);
    CHECK_EQ(preflight(0x10U, 4, one_s, one_s), 0U);
    CHECK_EQ(preflight(0x12U, 0, one_s, one_s), 0U);

    CHECK_EQ(preflight(0x10U, 0, 0x7fc00000U, one_s), 0U);
    CHECK_EQ(preflight(0x10U, 0, 1U, one_s), 0U);
    CHECK_EQ(preflight(0x11U, 0, one_d, 0x7ff8000000000000ULL), 0U);
    CHECK_EQ(preflight(0x11U, 0, one_d, 1ULL), 0U);

    constexpr u32 invalid_enabled = fs_mode | (1U << 11U);
    CHECK_EQ(preflight(0x10U, 0, 0x7f800000U, one_s, invalid_enabled), 0U);
    CHECK_EQ(preflight(0x11U, 1, one_d, 0x7ff0000000000000ULL, invalid_enabled), 0U);
    CHECK_EQ(preflight(0x10U, 3, 0U, 0x80000000U, invalid_enabled), 0U);
    CHECK(preflight(0x10U, 0, std::bit_cast<u32>(2.5f), std::bit_cast<u32>(3.5f), invalid_enabled) != 0U);
    CHECK(preflight(0x11U, 3, one_d, 0U, invalid_enabled) != 0U);

    // With two fractional significands, these exponent sums can enter the flushed
    // underflow path whose MUL latency differs from the ordinary arithmetic latency.
    CHECK_EQ(preflight(0x10U, 2, 0x00800001U, 0x3e800001U), 0U);
    CHECK_EQ(preflight(0x11U, 2, 0x0010000000000001ULL, 0x3fd0000000000001ULL), 0U);

    // A power of two is already the architectural one-cycle MUL shortcut, so it is
    // safe even in the same low-exponent range.
    CHECK_EQ(preflight(0x10U, 2, 0x00800001U, std::bit_cast<u32>(0.5f)), 2U);
    CHECK_EQ(preflight(0x11U, 2, 0x0010000000000001ULL, std::bit_cast<u64>(0.5)), 2U);

    // Exactly at the conservative exponent threshold, both fractional operands use
    // the normal multiplication latency.
    CHECK_EQ(preflight(0x10U, 2, 0x00800001U, 0x3f800001U), 5U);
    CHECK_EQ(preflight(0x11U, 2, 0x0010000000000001ULL, 0x3ff0000000000001ULL), 8U);
}

TEST(fpu_nontrapping_arithmetic_preflight_is_observation_only_and_preserves_host_fp_state) {
    System system;
    const u32 instruction = arithmetic_instruction(0x10U, 3U);
    prepare(system, false, (1U << 24U) | 3U, instruction);
    for (unsigned index = 0; index < system.cpu.fpu.registers.size(); ++index)
        system.cpu.fpu.registers[index] = 0x1020304050607080ULL + index * 0x0101010101010101ULL;
    set_operands(system.cpu.fpu, false, 0x10U, std::bit_cast<u32>(2.5f), std::bit_cast<u32>(3.5f));

    const auto registers = system.cpu.fpu.registers;
    const auto gpr = system.cpu.gpr;
    const auto cp0 = system.cpu.cp0;
    const u32 control = system.cpu.fpu.control;
    const u64 pc = system.cpu.pc;
    const u64 next_pc = system.cpu.next_pc;
    const u64 cycles = system.cpu.cycles;
    const u64 instruction_count = system.cpu.instruction_count;
    const bool exception_pending = system.cpu.exception_pending;
    const bool frozen = system.cpu.frozen;

    fenv_t original_environment{};
    CHECK_EQ(std::fegetenv(&original_environment), 0);
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned original_mxcsr = _mm_getcsr();
#endif
    struct RestoreEnvironment {
        const fenv_t& environment;
#if defined(__x86_64__) || defined(_M_X64)
        unsigned mxcsr;
#endif
        ~RestoreEnvironment() {
            static_cast<void>(std::fesetenv(&environment));
#if defined(__x86_64__) || defined(_M_X64)
            _mm_setcsr(mxcsr);
#endif
        }
    } restore{original_environment
#if defined(__x86_64__) || defined(_M_X64)
              ,
              original_mxcsr
#endif
    };

    CHECK_EQ(std::fesetenv(FE_DFL_ENV), 0);
    CHECK_EQ(std::fesetround(FE_DOWNWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_INVALID | FE_INEXACT), 0);
    const int expected_rounding = std::fegetround();
    const int expected_exceptions = std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned expected_mxcsr = _mm_getcsr();
#endif

    CHECK_EQ(system.cpu.fpu.nontrapping_arithmetic_cycles(instruction), 29U);
    CHECK_EQ(std::fegetround(), expected_rounding);
    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), expected_exceptions);
#if defined(__x86_64__) || defined(_M_X64)
    CHECK_EQ(_mm_getcsr(), expected_mxcsr);
#endif
    CHECK(system.cpu.fpu.registers == registers);
    CHECK(system.cpu.gpr == gpr);
    CHECK(system.cpu.cp0 == cp0);
    CHECK_EQ(system.cpu.fpu.control, control);
    CHECK_EQ(system.cpu.pc, pc);
    CHECK_EQ(system.cpu.next_pc, next_pc);
    CHECK_EQ(system.cpu.cycles, cycles);
    CHECK_EQ(system.cpu.instruction_count, instruction_count);
    CHECK_EQ(system.cpu.exception_pending, exception_pending);
    CHECK_EQ(system.cpu.frozen, frozen);
}
