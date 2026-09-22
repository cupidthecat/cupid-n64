#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <memory>

namespace {

using namespace cupid;

constexpr u32 cop1_transfer(unsigned operation, unsigned rt, unsigned fs) {
    return (0x11U << 26) | (operation << 21) | (rt << 16) | (fs << 11);
}

constexpr u32 cop1_format(unsigned format, unsigned ft, unsigned fs, unsigned fd, unsigned function) {
    return (0x11U << 26) | (format << 21) | (ft << 16) | (fs << 11) | (fd << 6) | function;
}

constexpr u32 cop1_branch(unsigned condition, s16 displacement) {
    return (0x11U << 26) | (8U << 21) | (condition << 16) | std::bit_cast<u16>(displacement);
}

constexpr u32 immediate(unsigned opcode, unsigned rs, unsigned rt, u16 value) {
    return (opcode << 26) | (rs << 21) | (rt << 16) | value;
}

std::unique_ptr<System> machine(bool full_registers = true) {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    system->cpu.write_cop0(12, 0x20000000U | (full_registers ? 0x04000000U : 0U));
    system->cpu.set_pc(0xffffffffa0001000ULL);
    return system;
}

u32 exception_code(const Cpu& cpu) {
    return static_cast<u32>((cpu.cp0[13] >> 2) & 31U);
}

void instruction(System& system, u32 address, u32 value) {
    system.bus.write(address, 4, value);
}

} // namespace

TEST(fpu_fr_mode_distinguishes_transfer_lanes_and_arithmetic_sources) {
    auto system = machine(false);
    auto& fpu = system->cpu.fpu;

    fpu.registers[2] = 0x1122334455667788ULL;
    CHECK_EQ(fpu.read_word(2), 0x55667788U);
    CHECK_EQ(fpu.read_word(3), 0x11223344U);
    CHECK_EQ(fpu.read_doubleword(3), 0x1122334455667788ULL);
    fpu.write_word(3, 0xaabbccddU);
    CHECK_EQ(fpu.registers[2], 0xaabbccdd55667788ULL);

    fpu.registers[28] = 0x1234567800000000ULL | std::bit_cast<u32>(-10.0f);
    fpu.registers[29] = std::bit_cast<u32>(10.0f);
    fpu.registers[30] = 0x7654321000000000ULL | std::bit_cast<u32>(16.0f);
    fpu.registers[31] = std::bit_cast<u32>(-16.0f);
    fpu.execute(cop1_format(0x10, 30, 29, 0, 0x00));
    fpu.execute(cop1_format(0x10, 31, 29, 1, 0x00));
    CHECK_EQ(fpu.registers[0], static_cast<u64>(std::bit_cast<u32>(6.0f)));
    CHECK_EQ(fpu.registers[1], static_cast<u64>(std::bit_cast<u32>(-26.0f)));

    fpu.execute(cop1_format(0x10, 0, 31, 7, 0x06));
    CHECK_EQ(fpu.registers[7], fpu.registers[30]);
}

TEST(fpu_full_mode_word_writes_preserve_transfer_upper_half_but_results_clear_it) {
    auto system = machine();
    auto& fpu = system->cpu.fpu;
    fpu.registers[4] = 0x1234567800000000ULL;
    fpu.write_word(4, std::bit_cast<u32>(3.0f));
    CHECK_EQ(fpu.registers[4] >> 32, 0x12345678ULL);
    fpu.registers[5] = std::bit_cast<u32>(4.0f);
    fpu.execute(cop1_format(0x10, 5, 4, 4, 0x00));
    CHECK_EQ(fpu.registers[4], static_cast<u64>(std::bit_cast<u32>(7.0f)));
}

TEST(fpu_control_register_mask_and_software_exceptions_match_fcsr) {
    auto system = machine();
    auto& cpu = system->cpu;
    auto& fpu = cpu.fpu;

    cpu.execute(cop1_transfer(0x02, 2, 0));
    CHECK_EQ(cpu.gpr[2], 0x00000a00ULL);
    cpu.gpr[1] = 0xffffffff0180007fULL;
    cpu.execute(cop1_transfer(0x06, 1, 31));
    CHECK_EQ(fpu.control, 0x0180007fU);
    CHECK(!cpu.exception_pending);
    cpu.execute(cop1_transfer(0x02, 2, 31));
    CHECK_EQ(cpu.gpr[2], 0x0180007fULL);

    auto trapping = machine();
    trapping->cpu.gpr[1] = (1U << 14) | (1U << 9);
    trapping->cpu.execute(cop1_transfer(0x06, 1, 31));
    CHECK(trapping->cpu.exception_pending);
    CHECK_EQ(exception_code(trapping->cpu), 15U);
    CHECK_EQ(trapping->cpu.fpu.control, (1U << 14) | (1U << 9));
}

TEST(fpu_maskable_exception_sets_sticky_only_when_disabled) {
    auto system = machine();
    auto& fpu = system->cpu.fpu;
    fpu.write_doubleword(2, std::bit_cast<u64>(1.0));
    fpu.write_doubleword(4, std::bit_cast<u64>(0.0));
    fpu.execute(cop1_format(0x11, 4, 2, 6, 0x03));
    CHECK_EQ(fpu.registers[6], std::bit_cast<u64>(std::numeric_limits<double>::infinity()));
    CHECK((fpu.control & (1U << 15)) != 0);
    CHECK((fpu.control & (1U << 5)) != 0);
    CHECK(!system->cpu.exception_pending);

    auto enabled = machine();
    enabled->cpu.fpu.control = 1U << 10;
    enabled->cpu.fpu.registers[2] = std::bit_cast<u64>(1.0);
    enabled->cpu.fpu.registers[4] = std::bit_cast<u64>(0.0);
    enabled->cpu.fpu.registers[6] = 0xfeedfaceULL;
    enabled->cpu.fpu.execute(cop1_format(0x11, 4, 2, 6, 0x03));
    CHECK(enabled->cpu.exception_pending);
    CHECK_EQ(exception_code(enabled->cpu), 15U);
    CHECK((enabled->cpu.fpu.control & (1U << 15)) != 0);
    CHECK((enabled->cpu.fpu.control & (1U << 5)) == 0);
    CHECK_EQ(enabled->cpu.fpu.registers[6], 0xfeedfaceULL);
}

TEST(fpu_nan_encodings_follow_vr4300_invalid_and_unimplemented_rules) {
    auto system = machine();
    auto& fpu = system->cpu.fpu;
    fpu.registers[2] = 0x7fc00000U;
    fpu.registers[4] = std::bit_cast<u32>(1.0f);
    fpu.execute(cop1_format(0x10, 4, 2, 6, 0x00));
    CHECK_EQ(fpu.registers[6], 0x7fbfffffULL);
    CHECK((fpu.control & (1U << 16)) != 0);
    CHECK((fpu.control & (1U << 6)) != 0);
    CHECK(!system->cpu.exception_pending);

    auto unimplemented = machine();
    unimplemented->cpu.fpu.registers[2] = 0x7f800001U;
    unimplemented->cpu.fpu.registers[4] = std::bit_cast<u32>(1.0f);
    unimplemented->cpu.fpu.registers[6] = 0x12345678ULL;
    unimplemented->cpu.fpu.execute(cop1_format(0x10, 4, 2, 6, 0x00));
    CHECK(unimplemented->cpu.exception_pending);
    CHECK_EQ(exception_code(unimplemented->cpu), 15U);
    CHECK((unimplemented->cpu.fpu.control & (1U << 17)) != 0);
    CHECK_EQ(unimplemented->cpu.fpu.registers[6], 0x12345678ULL);
}

TEST(fpu_subnormal_outputs_require_flush_mode_and_obey_round_direction) {
    auto rejected = machine();
    rejected->cpu.fpu.registers[2] = 0x00800000U;
    rejected->cpu.fpu.registers[4] = std::bit_cast<u32>(0.5f);
    rejected->cpu.fpu.execute(cop1_format(0x10, 4, 2, 6, 0x02));
    CHECK(rejected->cpu.exception_pending);
    CHECK((rejected->cpu.fpu.control & (1U << 17)) != 0);

    auto nearest = machine();
    nearest->cpu.fpu.control = 1U << 24;
    nearest->cpu.fpu.registers[2] = 0x00800000U;
    nearest->cpu.fpu.registers[4] = std::bit_cast<u32>(0.5f);
    nearest->cpu.fpu.execute(cop1_format(0x10, 4, 2, 6, 0x02));
    CHECK_EQ(nearest->cpu.fpu.registers[6], 0ULL);
    CHECK((nearest->cpu.fpu.control & ((1U << 13) | (1U << 12))) == ((1U << 13) | (1U << 12)));
    CHECK((nearest->cpu.fpu.control & ((1U << 3) | (1U << 2))) == ((1U << 3) | (1U << 2)));

    auto upward = machine();
    upward->cpu.fpu.control = (1U << 24) | 2U;
    upward->cpu.fpu.registers[2] = 0x00800000U;
    upward->cpu.fpu.registers[4] = std::bit_cast<u32>(0.5f);
    upward->cpu.fpu.execute(cop1_format(0x10, 4, 2, 6, 0x02));
    CHECK_EQ(upward->cpu.fpu.registers[6], 0x00800000ULL);
}

TEST(fpu_integer_conversions_use_requested_rounding_without_host_ub) {
    auto system = machine();
    auto& fpu = system->cpu.fpu;
    fpu.registers[2] = std::bit_cast<u32>(2.5f);
    fpu.execute(cop1_format(0x10, 0, 2, 4, 0x24));
    CHECK_EQ(fpu.registers[4], 2ULL);
    CHECK((fpu.control & (1U << 12)) != 0);

    fpu.control = 2U;
    fpu.execute(cop1_format(0x10, 0, 2, 4, 0x24));
    CHECK_EQ(fpu.registers[4], 3ULL);
    fpu.control = 3U;
    fpu.execute(cop1_format(0x10, 0, 2, 4, 0x0c));
    CHECK_EQ(fpu.registers[4], 2ULL);

    auto overflow = machine();
    overflow->cpu.fpu.registers[2] = std::bit_cast<u64>(2147483647.75);
    overflow->cpu.fpu.registers[4] = 0xaaaaaaaa55555555ULL;
    overflow->cpu.fpu.execute(cop1_format(0x11, 0, 2, 4, 0x24));
    CHECK(overflow->cpu.exception_pending);
    CHECK((overflow->cpu.fpu.control & (1U << 17)) != 0);
    CHECK_EQ(overflow->cpu.fpu.registers[4], 0xaaaaaaaa55555555ULL);
}

TEST(fpu_long_conversion_implements_documented_precision_boundary) {
    auto from_float = machine();
    from_float->cpu.fpu.registers[2] = std::bit_cast<u64>(-0x1p53);
    from_float->cpu.fpu.registers[4] = 0x1234ULL;
    from_float->cpu.fpu.execute(cop1_format(0x11, 0, 2, 4, 0x25));
    CHECK(from_float->cpu.exception_pending);
    CHECK((from_float->cpu.fpu.control & (1U << 17)) != 0);
    CHECK_EQ(from_float->cpu.fpu.registers[4], 0x1234ULL);

    auto from_long = machine();
    from_long->cpu.fpu.registers[2] = std::bit_cast<u64>(-static_cast<s64>(0x0080000000000000ULL));
    from_long->cpu.fpu.execute(cop1_format(0x15, 0, 2, 4, 0x21));
    CHECK(!from_long->cpu.exception_pending);
    CHECK_EQ(from_long->cpu.fpu.registers[4], std::bit_cast<u64>(-0x1p55));
    from_long->cpu.fpu.registers[2] = 0x0080000000000000ULL;
    from_long->cpu.fpu.execute(cop1_format(0x15, 0, 2, 4, 0x21));
    CHECK(from_long->cpu.exception_pending);
    CHECK((from_long->cpu.fpu.control & (1U << 17)) != 0);
}

TEST(fpu_compares_accept_subnormals_and_apply_nan_predicates) {
    auto system = machine();
    auto& fpu = system->cpu.fpu;
    fpu.registers[2] = 1U;
    fpu.registers[4] = 0U;
    fpu.execute(cop1_format(0x10, 4, 2, 0, 0x34));
    CHECK((fpu.control & (1U << 23)) == 0);
    CHECK((fpu.control & (1U << 17)) == 0);

    fpu.registers[2] = 0x7fc00000U;
    fpu.execute(cop1_format(0x10, 4, 2, 0, 0x32));
    CHECK((fpu.control & (1U << 16)) != 0);
    CHECK((fpu.control & (1U << 6)) != 0);
    CHECK((fpu.control & (1U << 23)) == 0);

    fpu.control = 0;
    fpu.registers[2] = 0x7f800001U;
    fpu.execute(cop1_format(0x10, 4, 2, 0, 0x31));
    CHECK((fpu.control & (1U << 16)) == 0);
    CHECK((fpu.control & (1U << 23)) != 0);
}

TEST(fpu_all_compare_predicates_match_finite_and_nan_truth_tables) {
    auto system = machine();
    auto& fpu = system->cpu.fpu;
    struct FiniteCase {
        float left;
        float right;
        bool less;
        bool equal;
    };
    const std::array<FiniteCase, 3> finite{{
        {1.0f, 2.0f, true, false},
        {2.0f, 2.0f, false, true},
        {3.0f, 2.0f, false, false},
    }};

    for (unsigned predicate = 0; predicate < 16; ++predicate) {
        const unsigned function = 0x30U + predicate;
        for (const auto& value : finite) {
            fpu.control = 0;
            fpu.registers[2] = std::bit_cast<u32>(value.left);
            fpu.registers[4] = std::bit_cast<u32>(value.right);
            fpu.execute(cop1_format(0x10, 4, 2, 0, function));
            const bool expected =
                (((function & 4U) != 0) && value.less) || (((function & 2U) != 0) && value.equal);
            CHECK_EQ((fpu.control & (1U << 23)) != 0, expected);
            CHECK_EQ(fpu.control & (1U << 16), 0U);
        }

        fpu.control = 0;
        fpu.registers[2] = 0x7f800001U;
        fpu.registers[4] = std::bit_cast<u32>(1.0f);
        fpu.execute(cop1_format(0x10, 4, 2, 0, function));
        CHECK_EQ((fpu.control & (1U << 23)) != 0, (function & 1U) != 0);
        CHECK_EQ((fpu.control & (1U << 16)) != 0, (function & 8U) != 0);

        fpu.control = 0;
        fpu.registers[2] = 0x7fc00000U;
        fpu.execute(cop1_format(0x10, 4, 2, 0, function));
        CHECK_EQ((fpu.control & (1U << 23)) != 0, (function & 1U) != 0);
        CHECK((fpu.control & (1U << 16)) != 0);
        CHECK((fpu.control & (1U << 6)) != 0);
    }
}

TEST(fpu_rounding_modes_cover_conversion_and_underflow_directions) {
    struct RoundingCase {
        u32 mode;
        s32 positive;
        s32 negative;
        u32 positive_underflow;
        u32 negative_underflow;
    };
    const std::array<RoundingCase, 4> cases{{
        {0, 4, -4, 0x00000000U, 0x80000000U},
        {1, 4, -4, 0x00000000U, 0x80000000U},
        {2, 5, -4, 0x00800000U, 0x80000000U},
        {3, 4, -5, 0x00000000U, 0x80800000U},
    }};

    for (const auto& value : cases) {
        auto system = machine();
        auto& fpu = system->cpu.fpu;
        fpu.control = value.mode;
        fpu.registers[2] = std::bit_cast<u32>(4.4f);
        fpu.execute(cop1_format(0x10, 0, 2, 4, 0x24));
        CHECK_EQ(std::bit_cast<s32>(static_cast<u32>(fpu.registers[4])), value.positive);
        fpu.control = value.mode;
        fpu.registers[2] = std::bit_cast<u32>(-4.4f);
        fpu.execute(cop1_format(0x10, 0, 2, 4, 0x24));
        CHECK_EQ(std::bit_cast<s32>(static_cast<u32>(fpu.registers[4])), value.negative);

        fpu.control = (1U << 24) | value.mode;
        fpu.registers[2] = 0x00800000U;
        fpu.registers[4] = std::bit_cast<u32>(0.5f);
        fpu.execute(cop1_format(0x10, 4, 2, 6, 0x02));
        CHECK_EQ(static_cast<u32>(fpu.registers[6]), value.positive_underflow);
        CHECK((fpu.control & ((1U << 13) | (1U << 12))) == ((1U << 13) | (1U << 12)));

        fpu.control = (1U << 24) | value.mode;
        fpu.registers[2] = 0x80800000U;
        fpu.registers[4] = std::bit_cast<u32>(0.5f);
        fpu.execute(cop1_format(0x10, 4, 2, 6, 0x02));
        CHECK_EQ(static_cast<u32>(fpu.registers[6]), value.negative_underflow);
    }
}

TEST(fpu_conversion_checks_post_round_positive_boundary) {
    auto nearest_ok = machine();
    nearest_ok->cpu.fpu.registers[2] = std::bit_cast<u64>(2147483647.4);
    nearest_ok->cpu.fpu.execute(cop1_format(0x11, 0, 2, 4, 0x24));
    CHECK(!nearest_ok->cpu.exception_pending);
    CHECK_EQ(static_cast<u32>(nearest_ok->cpu.fpu.registers[4]), 0x7fffffffU);

    auto upward_bad = machine();
    upward_bad->cpu.fpu.control = 2U;
    upward_bad->cpu.fpu.registers[2] = std::bit_cast<u64>(2147483647.4);
    upward_bad->cpu.fpu.registers[4] = 0xfeedfaceULL;
    upward_bad->cpu.fpu.execute(cop1_format(0x11, 0, 2, 4, 0x24));
    CHECK(upward_bad->cpu.exception_pending);
    CHECK((upward_bad->cpu.fpu.control & (1U << 17)) != 0);
    CHECK_EQ(upward_bad->cpu.fpu.registers[4], 0xfeedfaceULL);

    auto downward_ok = machine();
    downward_ok->cpu.fpu.control = 3U;
    downward_ok->cpu.fpu.registers[2] = std::bit_cast<u64>(2147483647.6);
    downward_ok->cpu.fpu.execute(cop1_format(0x11, 0, 2, 4, 0x24));
    CHECK(!downward_ok->cpu.exception_pending);
    CHECK_EQ(static_cast<u32>(downward_ok->cpu.fpu.registers[4]), 0x7fffffffU);

    auto nearest_bad = machine();
    nearest_bad->cpu.fpu.registers[2] = std::bit_cast<u64>(2147483647.6);
    nearest_bad->cpu.fpu.execute(cop1_format(0x11, 0, 2, 4, 0x24));
    CHECK(nearest_bad->cpu.exception_pending);
    CHECK((nearest_bad->cpu.fpu.control & (1U << 17)) != 0);
}

TEST(fpu_operations_restore_host_rounding_and_exception_environment) {
    const int original_rounding = std::fegetround();
    fenv_t original_environment{};
    CHECK_EQ(std::fegetenv(&original_environment), 0);
    CHECK_EQ(std::fesetround(FE_DOWNWARD), 0);
    CHECK_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(std::feraiseexcept(FE_INVALID), 0);

    auto system = machine();
    auto& fpu = system->cpu.fpu;
    fpu.control = 2U;
    fpu.registers[2] = std::bit_cast<u32>(1.0f);
    fpu.registers[4] = std::bit_cast<u32>(10.0f);
    fpu.execute(cop1_format(0x10, 4, 2, 6, 0x03));
    CHECK_EQ(std::fegetround(), FE_DOWNWARD);
    CHECK((std::fetestexcept(FE_INVALID) & FE_INVALID) != 0);

    CHECK_EQ(std::fesetenv(&original_environment), 0);
    CHECK_EQ(std::fegetround(), original_rounding);
}

TEST(fpu_reserved_formats_and_functions_preserve_cu1_priority) {
    const std::array<u32, 5> reserved{{
        (0x11U << 26) | (0x12U << 21),
        (0x11U << 26) | (0x10U << 21) | 0x10U,
        (0x11U << 26) | (0x11U << 21) | 0x21U,
        (0x11U << 26) | (0x14U << 21) | 0x24U,
        (0x11U << 26) | (0x08U << 21) | (4U << 16),
    }};
    for (u32 instruction : reserved) {
        auto enabled = machine();
        enabled->cpu.execute(instruction);
        CHECK(enabled->cpu.exception_pending);
        CHECK_EQ(exception_code(enabled->cpu), 15U);
        CHECK((enabled->cpu.fpu.control & (1U << 17)) != 0);

        auto disabled = machine();
        disabled->cpu.write_cop0(12, 0);
        disabled->cpu.execute(instruction);
        CHECK(disabled->cpu.exception_pending);
        CHECK_EQ(exception_code(disabled->cpu), 11U);
        CHECK_EQ((disabled->cpu.cp0[13] >> 28) & 3U, 1ULL);
        CHECK_EQ(disabled->cpu.fpu.control, 0U);
    }
}

TEST(fpu_reserved_decode_is_unimplemented_but_cu1_unusable_has_priority) {
    auto enabled = machine();
    enabled->cpu.fpu.execute((0x11U << 26) | (3U << 21));
    CHECK(enabled->cpu.exception_pending);
    CHECK_EQ(exception_code(enabled->cpu), 15U);
    CHECK((enabled->cpu.fpu.control & (1U << 17)) != 0);

    auto disabled = machine();
    disabled->cpu.write_cop0(12, 0);
    disabled->cpu.fpu.execute((0x11U << 26) | (3U << 21));
    CHECK(disabled->cpu.exception_pending);
    CHECK_EQ(exception_code(disabled->cpu), 11U);
    CHECK_EQ((disabled->cpu.cp0[13] >> 28) & 3U, 1ULL);
    CHECK_EQ(disabled->cpu.fpu.control, 0U);
}

TEST(fpu_load_store_paths_use_fr_transfer_mapping) {
    auto system = machine(false);
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0xffffffffa0004000ULL;
    cpu.fpu.registers[2] = 0x1122334455667788ULL;
    system->bus.write(0x4000, 4, 0xaabbccddU);
    cpu.execute(immediate(0x31, 1, 3, 0));
    CHECK_EQ(cpu.fpu.registers[2], 0xaabbccdd55667788ULL);
    cpu.execute(immediate(0x39, 1, 3, 4));
    CHECK_EQ(system->bus.read(0x4004, 4), 0xaabbccddULL);
    system->bus.write(0x4008, 8, 0x0123456789abcdefULL);
    cpu.execute(immediate(0x35, 1, 3, 8));
    CHECK_EQ(cpu.fpu.registers[2], 0x0123456789abcdefULL);
    cpu.execute(immediate(0x3d, 1, 3, 16));
    CHECK_EQ(system->bus.read(0x4010, 8), 0x0123456789abcdefULL);
}

TEST(fpu_branch_likely_annuls_and_nested_branch_uses_pending_target) {
    auto likely = machine();
    instruction(*likely, 0x1000, cop1_branch(3, 2));
    instruction(*likely, 0x1004, 0x0000000cU);
    instruction(*likely, 0x1008, immediate(9, 0, 1, 7));
    likely->cpu.step();
    CHECK_EQ(likely->cpu.pc, 0xffffffffa0001008ULL);
    likely->cpu.step();
    CHECK(!likely->cpu.exception_pending);
    CHECK_EQ(likely->cpu.gpr[1], 7ULL);

    auto nested = machine();
    nested->cpu.fpu.control = 1U << 23;
    instruction(*nested, 0x1000, immediate(4, 0, 0, 3));
    instruction(*nested, 0x1004, cop1_branch(1, 2));
    instruction(*nested, 0x1010, immediate(9, 0, 1, 5));
    instruction(*nested, 0x1018, immediate(9, 0, 2, 6));
    nested->cpu.step();
    nested->cpu.step();
    CHECK_EQ(nested->cpu.pc, 0xffffffffa0001010ULL);
    nested->cpu.step();
    CHECK_EQ(nested->cpu.pc, 0xffffffffa0001018ULL);
    nested->cpu.step();
    CHECK_EQ(nested->cpu.gpr[1], 5ULL);
    CHECK_EQ(nested->cpu.gpr[2], 6ULL);
}
