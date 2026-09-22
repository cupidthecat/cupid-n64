#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <limits>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u32 ViHSync = 0x0440001c;
constexpr u32 RiRefresh = 0x04700010;
constexpr u32 RiBankStatus = 0x0470001c;

void prepare(System& system, u64 pc, u32 instruction) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(pc);
    system.bus.write(static_cast<u32>(pc) & 0x1fffffffU, 4, instruction);
}

void set_system_fraction(System& system, u64 fraction) {
    constexpr std::array<u64, 3> cpu_cycles{0, 2, 1};
    system.advance(cpu_cycles[fraction]);
}

u64 line_cycles(const System& system, u32 hsync) {
    return ((static_cast<u64>(hsync) + 1U) * 62500000U + system.video_frequency() - 1U) /
           system.video_frequency();
}
} // namespace

TEST(cpu_cache_miss_sclock_sync_uses_all_three_system_fractions) {
    constexpr std::array<u64, 3> expected_cycles{42, 41, 41};
    for (u64 fraction = 0; fraction < 3; ++fraction) {
        System system;
        prepare(system, code, 0x8c220000); // LW v0, 0(at)
        system.cpu.gpr[1] = 0xffffffff80002000ULL;
        system.bus.write(0x2000, 4, 0x12345678);
        u64 ignored = 0;
        CHECK(system.cpu.read_memory(code, 4, ignored, true));
        set_system_fraction(system, fraction);

        system.cpu.step();

        CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
        CHECK_EQ(system.cpu.cycles, expected_cycles[fraction]);
    }
}

TEST(cpu_instruction_cache_miss_sclock_sync_uses_all_three_system_fractions) {
    constexpr std::array<u64, 3> expected_cycles{50, 49, 49};
    for (u64 fraction = 0; fraction < 3; ++fraction) {
        System system;
        prepare(system, code, 0);
        set_system_fraction(system, fraction);

        system.cpu.step();

        CHECK_EQ(system.cpu.cycles, expected_cycles[fraction]);
        CHECK_EQ(system.cpu.instruction_count, 1U);
    }
}

TEST(cpu_cache_miss_sclock_sync_phase_math_does_not_overflow) {
    constexpr std::array<u64, 3> expected{0, 0, 1};
    constexpr u64 maximum = std::numeric_limits<u64>::max();
    for (u64 fraction = 0; fraction < 3; ++fraction) {
        CHECK_EQ(cpu_timing::cache_miss_sclock_extra(0, fraction), expected[fraction]);
        CHECK_EQ(cpu_timing::cache_miss_sclock_extra(maximum, fraction), expected[fraction]);
    }
}

TEST(cpu_cold_instruction_miss_advances_count_before_the_instruction_executes) {
    System system;
    prepare(system, code, 0x40024800); // MFC0 v0, Count
    system.cpu.write_cop0(9, 0);

    system.cpu.step();

    CHECK_EQ(system.cpu.cycles, 50U);
    CHECK_EQ(system.cpu.gpr[2], 25U);
    CHECK_EQ(system.cpu.read_cop0(9), 25U);
}

TEST(cpu_speculative_instruction_miss_waits_until_after_the_older_count_sample) {
    System system;
    prepare(system, code + 28, 0x40024800); // MFC0 v0, Count
    system.bus.write(0x1020, 4, 0);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code + 28, 4, ignored, true));
    system.cpu.write_cop0(9, 0);

    system.cpu.step();

    CHECK_EQ(system.cpu.gpr[2], 0U);
    CHECK_EQ(system.cpu.cycles, 50U);
    CHECK_EQ(system.cpu.read_cop0(9), 25U);
}

TEST(cpu_speculative_instruction_miss_rechecks_refresh_at_the_completed_sclock_phase) {
    constexpr u32 hsync = 99;
    constexpr u32 recovery = 10;
    System system;
    prepare(system, code + 28, 0x40024800); // MFC0 v0, Count
    system.bus.write(0x1020, 4, 0);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code + 28, 4, ignored, true));
    system.cpu.write_cop0(9, 0);
    system.bus.write(ViHSync, 4, hsync);
    system.bus.write(RiRefresh, 4, 0x20000U | (recovery << 8U) | recovery);
    system.bus.write(RiBankStatus, 4, 0);
    system.bus.tick(line_cycles(system, hsync) - 33U);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);

    system.cpu.step();

    CHECK_EQ(system.cpu.gpr[2], 0U);
    CHECK_EQ(system.cpu.cycles, 65U);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
}

TEST(cpu_speculative_instruction_refills_preserve_two_line_store_prefetch) {
    System system;
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(code + 20);
    system.cpu.gpr[1] = code;
    system.cpu.gpr[2] = 0x12345678;
    system.cpu.gpr[3] = 0xffffffffa0003000ULL;
    system.bus.write(0x1014, 4, 0xbc200000); // CACHE IndexInvalidateI, 0(at)
    system.bus.write(0x1018, 4, 0xac620000); // SW v0, 0(v1)
    system.bus.write(0x101c, 4, 0);
    system.bus.write(0x1020, 4, 0);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code + 20, 4, ignored, true));

    system.cpu.step();
    system.cpu.step();

    CHECK_EQ(system.cpu.cycles, 98U);
    CHECK_EQ(system.bus.read(0x3000, 4), 0x12345678U);
    CHECK(system.cpu.read_memory(code + 28, 4, ignored, true));
    CHECK(system.cpu.read_memory(code + 32, 4, ignored, true));
}

TEST(cpu_cache_fill_instruction_uses_the_cache_miss_sclock_phase) {
    System system;
    prepare(system, code, 0xbc340000); // CACHE FillI, 0(at)
    system.cpu.gpr[1] = 0xffffffff80002000ULL;
    system.bus.write(0x2000, 4, 0x34030042);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code, 4, ignored, true));

    system.cpu.step();

    CHECK_EQ(system.cpu.cycles, 50U);
    u64 instruction = 0;
    CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, instruction, true));
    CHECK_EQ(instruction, 0x34030042U);
}
