#include "../../src/fpu/host_environment.hpp"
#include "../../src/fpu/integer_conversion.hpp"
#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <memory>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace {
using namespace cupid;

template <typename T, unsigned FractionBits, unsigned ExponentBits, unsigned Bias>
void check_conversion(std::conditional_t<sizeof(T) == 4, u32, u64> bits) {
    const T value = std::bit_cast<T>(bits);
    const double input = static_cast<double>(value);
    for (const bool long_result : {false, true}) {
        for (unsigned rounding = 0; rounding < 4; ++rounding) {
            const auto result =
                fpu_host::integer_from_bits<FractionBits, ExponentBits, Bias>(bits, long_result, rounding);
            bool supported = std::isfinite(input) && std::fpclassify(value) != FP_SUBNORMAL;
            supported = supported && (long_result ? input > -0x1p53 && input < 0x1p53
                                                  : input >= -0x1p31 && input < 0x1p31);
            if (!supported) {
                CHECK(!result);
                continue;
            }
            const double rounded = rounding == 0   ? std::nearbyint(input)
                                   : rounding == 1 ? std::trunc(input)
                                   : rounding == 2 ? std::ceil(input)
                                                   : std::floor(input);
            if (!long_result && (rounded < -2147483648.0 || rounded > 2147483647.0)) {
                CHECK(!result);
                continue;
            }
            CHECK(result.has_value());
            CHECK_EQ(result->bits, std::bit_cast<u64>(static_cast<s64>(rounded)));
            CHECK_EQ(result->inexact, rounded != input);
        }
    }
}

} // namespace

TEST(fpu_integer_bit_conversion_covers_every_exponent_and_rounding_boundary) {
    const fpu_host::ScopedEnvironment environment(FE_TONEAREST);
    constexpr std::array<u64, 8> fractions{0,
                                           1,
                                           0x0007ffffffffffffULL,
                                           0x0008000000000000ULL,
                                           0x0008000000000001ULL,
                                           0x000ffffffffffffeULL,
                                           0x000fffffffffffffULL,
                                           0x0005555555555555ULL};
    for (u64 sign : {0ULL, 0x8000000000000000ULL}) {
        for (u64 exponent = 0; exponent < 2048; ++exponent) {
            for (u64 fraction : fractions) {
                check_conversion<double, 52, 11, 1023>(sign | (exponent << 52U) | fraction);
                if (exponent < 256)
                    check_conversion<float, 23, 8, 127>(
                        static_cast<u32>((sign >> 32U) | (exponent << 23U) | (fraction >> 29U)));
            }
        }
    }
    for (double value :
         {2147483646.5, 2147483647.0, 2147483647.25, 2147483647.5, 2147483647.75, 2147483648.0, -2147483648.0,
          -2147483647.75, -2147483647.5, -2147483647.25, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5})
        check_conversion<double, 52, 11, 1023>(std::bit_cast<u64>(value));
    // Distinct low mantissa bits exercise discarded remainders at every scale.
    u64 random = 0x97f31a6d52c08be4ULL;
    for (unsigned index = 0; index < 10000; ++index) {
        random ^= random << 13U;
        random ^= random >> 7U;
        random ^= random << 17U;
        check_conversion<double, 52, 11, 1023>(random);
        check_conversion<float, 23, 8, 127>(static_cast<u32>(random));
    }
}

TEST(fpu_integer_conversions_keep_fixed_rounding_flags_and_source_aliases) {
    auto system = std::make_unique<System>();
    auto& cpu = system->cpu;
    for (const bool full_registers : {false, true}) {
        cpu.write_cop0(12, full_registers ? 0x34000000U : 0x30000000U);
        for (unsigned format : {0x10U, 0x11U}) {
            for (unsigned function : {8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 0x24U, 0x25U}) {
                for (unsigned rounding = 0; rounding < 4; ++rounding) {
                    auto& fpu = cpu.fpu;
                    fpu.registers.fill(0xdeadbeef00000000ULL);
                    fpu.registers[full_registers ? 3 : 2] =
                        format == 0x10U ? 0xc0200000ULL : 0xc004000000000000ULL; // -2.5.
                    fpu.control = rounding | 0x0003f000U | (1U << 5U) | (1U << 23U);
                    cpu.exception_pending = false;
                    const u32 instruction =
                        0x44000000U | (format << 21U) | (3U << 11U) | (3U << 6U) | function;
                    fpu.execute(instruction);
                    const unsigned effective = function < 16 ? function & 3U : rounding;
                    const s64 expected = effective == 3 ? -3 : -2;
                    const bool long_result = function < 16 ? function < 12 : function == 0x25U;
                    CHECK_EQ(fpu.registers[3], long_result ? std::bit_cast<u64>(expected)
                                                           : static_cast<u64>(static_cast<u32>(expected)));
                    CHECK_EQ(fpu.control, rounding | (1U << 12U) | (1U << 5U) | (1U << 2U) | (1U << 23U));
                    CHECK(!cpu.exception_pending);
                }
            }
        }
    }
}

#if defined(__x86_64__) || defined(_M_X64)
TEST(fpu_integer_conversion_preserves_enabled_host_traps_and_denormal_controls) {
    const fpu_host::ScopedEnvironment environment(FE_TONEAREST);
    auto system = std::make_unique<System>();
    system->cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system->cpu.fpu;
    const unsigned saved = _mm_getcsr();
    // Enable host invalid/inexact traps while retaining sticky flags and DAZ/FTZ.
    const unsigned hostile = (saved | 0x8040U | 0x21U) & ~0x1080U;
    for (unsigned format : {0x10U, 0x11U}) {
        fpu.registers[2] = format == 0x10U ? 0x00800000ULL : 0x0010000000000000ULL;
        fpu.control = 2; // CVT.W rounds the smallest normal positive input upward to one.
        _mm_setcsr(hostile);
        fpu.execute(0x44000000U | (format << 21U) | (2U << 11U) | (4U << 6U) | 0x24U);
        const unsigned actual = _mm_getcsr();
        _mm_setcsr(saved);
        CHECK_EQ(actual, hostile);
        CHECK_EQ(fpu.registers[4], 1U);
        CHECK_EQ(fpu.control, 2U | (1U << 12U) | (1U << 2U));
        CHECK(!system->cpu.exception_pending);
    }
}
#endif
