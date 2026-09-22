#include "../../src/fpu/host_environment.hpp"
#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <bit>
#include <cfenv>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace {
using namespace cupid;

struct FakeEnvironmentApi {
    static inline bool save_succeeds{true};
    static inline bool default_succeeds{true};
    static inline int saved_rounding{FE_DOWNWARD};
    static inline unsigned get_environment_calls{};
    static inline unsigned set_default_calls{};
    static inline unsigned restore_environment_calls{};
    static inline unsigned get_rounding_calls{};
    static inline std::array<int, 4> rounding_writes{};
    static inline unsigned rounding_write_count{};

    static void reset() {
        save_succeeds = true;
        default_succeeds = true;
        saved_rounding = FE_DOWNWARD;
        get_environment_calls = 0;
        set_default_calls = 0;
        restore_environment_calls = 0;
        get_rounding_calls = 0;
        rounding_writes = {};
        rounding_write_count = 0;
    }

    static int get_environment(fenv_t*) noexcept {
        ++get_environment_calls;
        return save_succeeds ? 0 : -1;
    }

    static int set_environment(const fenv_t* environment) noexcept {
        if (environment == FE_DFL_ENV) {
            ++set_default_calls;
            return default_succeeds ? 0 : -1;
        }
        ++restore_environment_calls;
        return 0;
    }

    static int get_rounding() noexcept {
        ++get_rounding_calls;
        return saved_rounding;
    }

    static int set_rounding(int rounding) noexcept {
        rounding_writes[rounding_write_count++] = rounding;
        return 0;
    }
};

class HostEnvironment {
  public:
    HostEnvironment() {
        CHECK_EQ(std::fegetenv(&saved_), 0);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        saved_control_ = _mm_getcsr();
#endif
        CHECK_EQ(std::fesetenv(FE_DFL_ENV), 0);
    }

    ~HostEnvironment() {
        std::fesetenv(&saved_);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        _mm_setcsr(saved_control_);
#endif
    }

  private:
    fenv_t saved_{};
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    unsigned saved_control_{};
#endif
};

constexpr u32 operation(unsigned format, unsigned function) {
    return 0x44000000U | (format << 21U) | (4U << 16U) | (2U << 11U) | (6U << 6U) | function;
}

constexpr u32 compare_operation(unsigned format, unsigned ft, unsigned fs, unsigned function) {
    return 0x44000000U | (format << 21U) | ((ft & 31U) << 16U) | ((fs & 31U) << 11U) | function;
}

struct CompareCase {
    u64 left;
    u64 right;
    bool less;
    bool equal;
    bool unordered;
    bool invalid_always;
};

} // namespace

TEST(fpu_host_environment_avoids_round_query_after_full_save_and_reuses_default_nearest) {
    using Scope = fpu_host::ScopedEnvironment<FakeEnvironmentApi>;

    FakeEnvironmentApi::reset();
    {
        const Scope scope(FE_TONEAREST);
        CHECK_EQ(FakeEnvironmentApi::get_environment_calls, 1U);
        CHECK_EQ(FakeEnvironmentApi::set_default_calls, 1U);
        CHECK_EQ(FakeEnvironmentApi::get_rounding_calls, 0U);
        CHECK_EQ(FakeEnvironmentApi::rounding_write_count, 0U);
    }
    CHECK_EQ(FakeEnvironmentApi::restore_environment_calls, 1U);

    FakeEnvironmentApi::reset();
    {
        const Scope scope(FE_UPWARD);
        CHECK_EQ(FakeEnvironmentApi::get_rounding_calls, 0U);
        CHECK_EQ(FakeEnvironmentApi::rounding_write_count, 1U);
        CHECK_EQ(FakeEnvironmentApi::rounding_writes[0], FE_UPWARD);
    }
    CHECK_EQ(FakeEnvironmentApi::restore_environment_calls, 1U);
}

TEST(fpu_host_environment_preserves_rounding_fallbacks_when_environment_operations_fail) {
    using Scope = fpu_host::ScopedEnvironment<FakeEnvironmentApi>;

    FakeEnvironmentApi::reset();
    FakeEnvironmentApi::default_succeeds = false;
    {
        const Scope scope(FE_TONEAREST);
        CHECK_EQ(FakeEnvironmentApi::get_rounding_calls, 0U);
        CHECK_EQ(FakeEnvironmentApi::rounding_write_count, 1U);
        CHECK_EQ(FakeEnvironmentApi::rounding_writes[0], FE_TONEAREST);
    }
    CHECK_EQ(FakeEnvironmentApi::restore_environment_calls, 1U);

    FakeEnvironmentApi::reset();
    FakeEnvironmentApi::save_succeeds = false;
    FakeEnvironmentApi::saved_rounding = FE_DOWNWARD;
    {
        const Scope scope(FE_UPWARD);
        CHECK_EQ(FakeEnvironmentApi::set_default_calls, 0U);
        CHECK_EQ(FakeEnvironmentApi::get_rounding_calls, 1U);
        CHECK_EQ(FakeEnvironmentApi::rounding_write_count, 1U);
        CHECK_EQ(FakeEnvironmentApi::rounding_writes[0], FE_UPWARD);
    }
    CHECK_EQ(FakeEnvironmentApi::restore_environment_calls, 0U);
    CHECK_EQ(FakeEnvironmentApi::rounding_write_count, 2U);
    CHECK_EQ(FakeEnvironmentApi::rounding_writes[1], FE_DOWNWARD);
}

TEST(fpu_nearest_guest_operation_restores_dirty_host_rounding_and_exception_flags) {
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;

    CHECK_EQ(std::fesetround(FE_DOWNWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_OVERFLOW | FE_INEXACT), 0);
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const unsigned dirty_control = (_mm_getcsr() | static_cast<unsigned>(_MM_FLUSH_ZERO_ON) | 0x0040U) &
                                   ~static_cast<unsigned>(_MM_MASK_DIV_ZERO);
    _mm_setcsr(dirty_control);
#endif

    fpu.control = 0;
    fpu.registers[2] = std::bit_cast<u64>(1.25);
    fpu.registers[4] = std::bit_cast<u64>(2.5);
    fpu.execute(operation(0x11U, 0));
    CHECK_EQ(fpu.registers[6], std::bit_cast<u64>(3.75));
    CHECK_EQ(std::fegetround(), FE_DOWNWARD);
    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    CHECK_EQ(_mm_getcsr(), dirty_control);
#endif
}

TEST(fpu_abs_and_neg_keep_dirty_host_environment_untouched) {
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;

    CHECK_EQ(std::fesetround(FE_UPWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_OVERFLOW | FE_INEXACT), 0);
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const unsigned dirty_control = (_mm_getcsr() | static_cast<unsigned>(_MM_FLUSH_ZERO_ON) | 0x0040U) &
                                   ~static_cast<unsigned>(_MM_MASK_DIV_ZERO);
    _mm_setcsr(dirty_control);
#endif

    for (const auto& [function, input, expected] :
         std::array{std::array<u32, 3>{0x05U, 0xbf800000U, 0x3f800000U},
                    std::array<u32, 3>{0x07U, 0x3f800000U, 0xbf800000U}}) {
        fpu.control = 0x0003f000U;
        fpu.registers[2] = input;
        fpu.registers[6] = 0;
        fpu.execute(operation(0x10U, function));
        CHECK_EQ(fpu.registers[6], expected);
        CHECK_EQ(fpu.control & 0x0003f000U, 0U);
        CHECK_EQ(std::fegetround(), FE_UPWARD);
        CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        CHECK_EQ(_mm_getcsr(), dirty_control);
#endif
    }
}

TEST(fpu_early_exception_paths_restore_dirty_host_environment) {
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;

    CHECK_EQ(std::fesetround(FE_DOWNWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_OVERFLOW | FE_INEXACT), 0);
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const unsigned dirty_control = (_mm_getcsr() | static_cast<unsigned>(_MM_FLUSH_ZERO_ON) | 0x0040U) &
                                   ~static_cast<unsigned>(_MM_MASK_DIV_ZERO);
    _mm_setcsr(dirty_control);
#endif

    fpu.control = 1U << 11U;
    fpu.registers[2] = 0x7fc00000U;
    fpu.registers[4] = 0x3f800000U;
    system.cpu.exception_pending = false;
    fpu.execute(operation(0x10U, 0x32U));
    CHECK(system.cpu.exception_pending);
    CHECK_EQ(std::fegetround(), FE_DOWNWARD);
    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    CHECK_EQ(_mm_getcsr(), dirty_control);
#endif

    fpu.control = 0;
    fpu.registers[2] = 1U;
    system.cpu.exception_pending = false;
    fpu.execute(operation(0x10U, 0x05U));
    CHECK(system.cpu.exception_pending);
    CHECK_EQ(fpu.control & (1U << 17U), 1U << 17U);
    CHECK_EQ(std::fegetround(), FE_DOWNWARD);
    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    CHECK_EQ(_mm_getcsr(), dirty_control);
#endif
}

TEST(fpu_compare_trap_restores_host_environment_after_younger_uncached_fetch_callback) {
    constexpr u64 code = 0xffffffff80001000ULL;
    constexpr u64 target = 0xffffffffa0002000ULL;
    HostEnvironment host;
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    auto& fpu = cpu.fpu;
    cpu.write_cop0(12, 0x34000000U);
    cpu.set_pc(code);
    cpu.gpr[3] = target;
    fpu.control = 1U << 11U;
    fpu.registers[2] = 0x7fc00000U;
    fpu.registers[4] = 0x3f800000U;
    system.bus.write(0x1000, 4, 0x00600008U); // JR v1
    system.bus.write(0x1004, 4, operation(0x10U, 0x32U));
    system.bus.write(0x2000, 4, 0x48000000U); // COP2 opcode sampled by the older FPU exception.

    system.bus.write(0x04400000, 4, 0x303U);
    system.bus.write(0x04400008, 4, 16U);
    system.bus.write(0x04400018, 4, 4U);
    system.bus.write(0x0440001c, 4, 99U);
    system.bus.write(0x04400020, 4, (100U << 16U) | 100U);
    system.bus.write(0x04400024, 4, (100U << 16U) | 100U);
    system.bus.write(0x04400028, 4, (2U << 16U) | 4U);
    system.bus.write(0x04400030, 4, 1024U);
    system.bus.write(0x04400034, 4, 1024U);

    u64 ignored = 0;
    CHECK(cpu.read_memory(code, 4, ignored, true));
    unsigned callbacks = 0;
    system.bus.set_video_output([&](VideoField) {
        ++callbacks;
        CHECK_EQ(std::fesetround(FE_TOWARDZERO), 0);
        CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
        CHECK_EQ(std::feraiseexcept(FE_INVALID), 0);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        _mm_setcsr(0x1f80U | static_cast<unsigned>(_MM_FLUSH_ZERO_ON) | 0x0040U |
                   static_cast<unsigned>(_MM_ROUND_TOWARD_ZERO));
#endif
    });

    cpu.step();
    CHECK(!cpu.exception_pending);
    const u64 first_line = (100ULL * 62500000ULL + system.video_frequency() - 1U) / system.video_frequency();
    const u64 clock = system.bus.output_clock();
    CHECK(clock < first_line);
    system.bus.tick(first_line - clock - 1U);
    CHECK_EQ(callbacks, 0U);

    CHECK_EQ(std::fesetround(FE_DOWNWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_OVERFLOW | FE_INEXACT), 0);
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const unsigned dirty_control = (_mm_getcsr() | static_cast<unsigned>(_MM_FLUSH_ZERO_ON) | 0x0040U) &
                                   ~static_cast<unsigned>(_MM_MASK_DIV_ZERO);
    _mm_setcsr(dirty_control);
#endif

    cpu.step();
    CHECK(cpu.exception_pending);
    CHECK_EQ(callbacks, 1U);
    CHECK_EQ(cpu.read_cop0(13) & 0x3000007cU, 0x2000003cU);
    CHECK_EQ(std::fegetround(), FE_DOWNWARD);
    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    CHECK_EQ(_mm_getcsr(), dirty_control);
#endif
}

TEST(fpu_compare_integer_path_matches_all_predicates_without_touching_host_environment) {
    HostEnvironment host;
    System system;
    system.cpu.write_cop0(12, 0x34000000U);
    auto& fpu = system.cpu.fpu;

    CHECK_EQ(std::fesetround(FE_UPWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_OVERFLOW | FE_INEXACT), 0);
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const unsigned dirty_control = (_mm_getcsr() | static_cast<unsigned>(_MM_FLUSH_ZERO_ON) | 0x0040U) &
                                   ~static_cast<unsigned>(_MM_MASK_DIV_ZERO);
    _mm_setcsr(dirty_control);
#endif

    const std::array<CompareCase, 10> single_cases{{
        {0x00000000U, 0x80000000U, false, true, false, false},
        {0x80000000U, 0x00000000U, false, true, false, false},
        {0x00000001U, 0x00000000U, false, false, false, false},
        {0x80000001U, 0x80000000U, true, false, false, false},
        {0x3f800000U, 0x40000000U, true, false, false, false},
        {0xc0000000U, 0xbf800000U, true, false, false, false},
        {0xff800000U, 0xff7fffffU, true, false, false, false},
        {0x7f7fffffU, 0x7f800000U, true, false, false, false},
        {0x7fc00000U, 0x3f800000U, false, false, true, true},
        {0x3f800000U, 0x7f800001U, false, false, true, false},
    }};
    const std::array<CompareCase, 10> double_cases{{
        {0x0000000000000000ULL, 0x8000000000000000ULL, false, true, false, false},
        {0x8000000000000000ULL, 0x0000000000000000ULL, false, true, false, false},
        {0x0000000000000001ULL, 0x0000000000000000ULL, false, false, false, false},
        {0x8000000000000001ULL, 0x8000000000000000ULL, true, false, false, false},
        {0x3ff0000000000000ULL, 0x4000000000000000ULL, true, false, false, false},
        {0xc000000000000000ULL, 0xbff0000000000000ULL, true, false, false, false},
        {0xfff0000000000000ULL, 0xffefffffffffffffULL, true, false, false, false},
        {0x7fefffffffffffffULL, 0x7ff0000000000000ULL, true, false, false, false},
        {0x7ff8000000000000ULL, 0x3ff0000000000000ULL, false, false, true, true},
        {0x3ff0000000000000ULL, 0x7ff0000000000001ULL, false, false, true, false},
    }};

    const auto check_cases = [&](unsigned format, const auto& cases) {
        for (const CompareCase& test : cases) {
            for (unsigned predicate = 0; predicate < 16U; ++predicate) {
                const unsigned function = 0x30U + predicate;
                fpu.control = (1U << 24U) | 0x0003f000U | (1U << 23U);
                fpu.registers[2] = test.left;
                fpu.registers[4] = test.right;
                system.cpu.exception_pending = false;
                fpu.execute(operation(format, function));

                const bool condition = test.unordered ? (predicate & 1U) != 0
                                                      : (((predicate & 4U) != 0 && test.less) ||
                                                         ((predicate & 2U) != 0 && test.equal));
                const bool invalid = test.invalid_always || (test.unordered && (predicate & 8U) != 0);
                const u32 expected_control = (1U << 24U) | (static_cast<u32>(condition) << 23U) |
                                             (invalid ? (1U << 16U) | (1U << 6U) : 0U);
                CHECK_EQ(fpu.control, expected_control);
                CHECK(!system.cpu.exception_pending);
                CHECK_EQ(std::fegetround(), FE_UPWARD);
                CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                CHECK_EQ(_mm_getcsr(), dirty_control);
#endif
            }
        }
    };

    check_cases(0x10U, single_cases);
    check_cases(0x11U, double_cases);
}

TEST(fpu_compare_integer_path_preserves_fr_source_aliases) {
    HostEnvironment host;
    System system;
    auto& fpu = system.cpu.fpu;

    for (const unsigned format : {0x10U, 0x11U}) {
        const u64 one = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
        const u64 two = format == 0x10U ? 0x40000000ULL : 0x4000000000000000ULL;
        const u64 three = format == 0x10U ? 0x40400000ULL : 0x4008000000000000ULL;

        fpu.registers[2] = one;
        fpu.registers[3] = three;
        fpu.registers[5] = two;

        system.cpu.write_cop0(12, 0x30000000U);
        fpu.control = 0;
        fpu.execute(compare_operation(format, 5U, 3U, 0x34U));
        CHECK_EQ((fpu.control >> 23U) & 1U, 1U);

        system.cpu.write_cop0(12, 0x34000000U);
        fpu.control = 0;
        fpu.execute(compare_operation(format, 5U, 3U, 0x34U));
        CHECK_EQ((fpu.control >> 23U) & 1U, 0U);
    }
}
