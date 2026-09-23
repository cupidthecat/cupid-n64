#include "../../src/fpu/binary_arithmetic.hpp"
#include "test.hpp"

#include <array>
#include <bit>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
namespace {
template <typename T, typename U, std::size_t N>
void compare_arithmetic(const std::array<U, N>& operands, U exponent, U fraction) {
    fenv_t original{};
    CHECK_EQ(std::fegetenv(&original), 0);
    struct Restore {
        const fenv_t& environment;
        unsigned control;
        ~Restore() {
            std::fesetenv(&environment);
            _mm_setcsr(control);
        }
    } restore{original, _mm_getcsr()};
    constexpr int flags = FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT;
    for (const int rounding : {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD}) {
        for (unsigned function = 0; function < 4; ++function) {
            for (const U left : operands) {
                for (const U right : operands) {
                    CHECK_EQ(std::fesetenv(FE_DFL_ENV), 0);
                    CHECK_EQ(std::feraiseexcept(FE_INVALID | FE_INEXACT), 0);
                    const int prior_flags = std::fetestexcept(FE_ALL_EXCEPT);
                    const unsigned host = _mm_getcsr() | 0xe040U;
                    _mm_setcsr(host);
                    const auto actual = cupid::fpu_host::binary(function, std::bit_cast<T>(left),
                                                                std::bit_cast<T>(right), rounding);
                    CHECK_EQ(_mm_getcsr(), host);
                    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), prior_flags);
                    const auto expected = cupid::fpu_host::binary_portable(function, std::bit_cast<T>(left),
                                                                           std::bit_cast<T>(right), rounding);
                    CHECK_EQ(_mm_getcsr(), host);
                    const U a = std::bit_cast<U>(actual.value);
                    const U b = std::bit_cast<U>(expected.value);
                    const bool nan = (a & exponent) == exponent && (a & fraction) != 0;
                    if (nan)
                        CHECK((b & exponent) == exponent && (b & fraction) != 0);
                    else
                        CHECK_EQ(a, b);
                    CHECK_EQ(actual.exceptions & flags, expected.exceptions & flags);
                }
            }
        }
    }
}
} // namespace

TEST(fpu_binary_sse_matches_portable_rounding_flags_and_preserves_host_state) {
    compare_arithmetic<float>(std::array<std::uint32_t, 14>{0, 0x80000000U, 1, 0x007fffffU, 0x00800000U,
                                                            0x3f000001U, 0x3f800000U, 0x3f800001U,
                                                            0xbf800001U, 0x40000000U, 0x7f7fffffU,
                                                            0xff7fffffU, 0x7f800000U, 0xff800000U},
                              0x7f800000U, 0x007fffffU);
    compare_arithmetic<double>(
        std::array<std::uint64_t, 14>{
            0, 0x8000000000000000ULL, 1, 0x000fffffffffffffULL, 0x0010000000000000ULL, 0x3fe0000000000001ULL,
            0x3ff0000000000000ULL, 0x3ff0000000000001ULL, 0xbff0000000000001ULL, 0x4000000000000000ULL,
            0x7fefffffffffffffULL, 0xffefffffffffffffULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL},
        std::uint64_t{0x7ff0000000000000ULL}, std::uint64_t{0x000fffffffffffffULL});
}
#endif
