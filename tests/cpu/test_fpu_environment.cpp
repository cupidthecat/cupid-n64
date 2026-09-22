#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <cfenv>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace {
using namespace cupid;

class HostEnvironment {
  public:
    HostEnvironment() {
        CHECK_EQ(std::fegetenv(&saved_), 0);
        CHECK_EQ(std::fesetenv(FE_DFL_ENV), 0);
    }
    ~HostEnvironment() {
        std::fesetenv(&saved_);
    }

  private:
    fenv_t saved_{};
};

constexpr u32 operation(unsigned format, unsigned function) {
    return 0x44000000U | (format << 21U) | (4U << 16U) | (2U << 11U) | (6U << 6U) | function;
}

} // namespace

TEST(fpu_format_operations_restore_host_state_on_success_and_guest_exceptions) {
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;
    for (const int host_round : {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD}) {
        for (const unsigned function : {0U, 3U, 4U, 5U, 7U, 8U, 9U, 10U, 11U, 0x20U, 0x24U, 0x32U}) {
            for (const u64 operand : {0x3ff4000000000000ULL, 1ULL, 0x7ff8000000000000ULL}) {
                CHECK_EQ(std::fesetround(host_round), 0);
                CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
                CHECK_EQ(std::feraiseexcept(FE_OVERFLOW | FE_INEXACT), 0);
                const int flags = std::fetestexcept(FE_ALL_EXCEPT);
                fpu.control = 2;
                fpu.registers[2] = operand;
                fpu.registers[4] = 0x4004000000000000ULL;
                fpu.execute(operation(0x11U, function));
                CHECK_EQ(std::fegetround(), host_round);
                CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
            }
        }
    }
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
TEST(fpu_comparisons_preserve_subnormals_under_host_denormal_modes) {
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;
    for (unsigned format : {0x10U, 0x11U}) {
        const u64 sign = format == 0x10U ? 0x80000000ULL : 0x8000000000000000ULL;
        const u64 largest = format == 0x10U ? 0x007fffffULL : 0x000fffffffffffffULL;
        for (unsigned mode : {0x40U, 0U, 0x8000U, 0x8040U}) {
            for (u64 magnitude : std::array<u64, 2>{1, largest}) {
                for (bool negative : {false, true}) {
                    for (bool reversed : {false, true}) {
                        for (unsigned function = 0x30U; function <= 0x3fU; ++function) {
                            const u32 host_control = 0x1f80U | mode;
                            _mm_setcsr(host_control);
                            fpu.registers[reversed ? 4U : 2U] = magnitude | (negative ? sign : 0);
                            fpu.registers[reversed ? 2U : 4U] = 0;
                            fpu.control = 1U << 24U;
                            fpu.execute(operation(format, function));
                            const bool expected = (function & 4U) != 0 && (negative != reversed);
                            CHECK_EQ((fpu.control >> 23U) & 1U, static_cast<u32>(expected));
                            CHECK_EQ(fpu.control & 0x0003ffffU, 0U);
                            CHECK_EQ(_mm_getcsr(), host_control);
                        }
                    }
                }
            }
        }
    }
}

TEST(fpu_division_uses_guest_exception_enables_with_host_traps_enabled) {
    constexpr u64 sentinel = 0x123456789abcdef0ULL;
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;
    for (unsigned format : {0x10U, 0x11U}) {
        for (bool enabled : {false, true}) {
            const u32 host_control = 0x1f80U & ~static_cast<u32>(_MM_MASK_DIV_ZERO);
            _mm_setcsr(host_control);
            fpu.control = enabled ? 1U << 10U : 0U;
            fpu.registers[2] = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
            fpu.registers[4] = 0;
            fpu.registers[6] = sentinel;
            system.cpu.exception_pending = false;
            fpu.execute(operation(format, 3));
            CHECK_EQ(system.cpu.exception_pending, enabled);
            CHECK_EQ(fpu.registers[6], enabled           ? sentinel
                                       : format == 0x10U ? 0x7f800000ULL
                                                         : 0x7ff0000000000000ULL);
            CHECK_EQ(fpu.control, (1U << 15U) | (enabled ? 1U << 10U : 1U << 5U));
            CHECK_EQ(_mm_getcsr(), host_control);
        }
    }
}
#endif
