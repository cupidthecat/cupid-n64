#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <initializer_list>

namespace {
using namespace cupid;

void prepare(System& system, std::initializer_list<u32> instructions) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    u32 address = 0x1000;
    for (u32 instruction : instructions) {
        system.bus.write(address, 4, instruction);
        address += 4;
    }
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
    system.cpu.gpr[1] = 0xffffffff80002000ULL;
    system.bus.write(0x2000, 4, 0x12345678);
    CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, ignored));
}
} // namespace

TEST(cpu_cached_load_only_stalls_a_dependent_instruction) {
    System system;
    prepare(system, {0x8c220000, 0x24030007, 0x8c220000, 0x24430001});
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 1U);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 2U);
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 5U);
    CHECK_EQ(system.cpu.gpr[3], 0x12345679U);
}

TEST(cpu_load_interlock_checks_encoded_destination_but_ignores_zero) {
    System system;
    prepare(system, {0x8c220000, 0x3c020001, 0x8c200000, 0x24030007});
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 3U);
    CHECK_EQ(system.cpu.gpr[2], 0x10000U);
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 5U);
}

TEST(cpu_cop0_moves_have_issue_latency_and_load_interlocks) {
    System system;
    prepare(system, {0x40823800, 0x40023800, 0x24430001});
    system.cpu.gpr[2] = 123;
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 2U);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 3U);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 5U);
    CHECK_EQ(system.cpu.gpr[3], 124U);
}

TEST(cpu_annulled_delay_slot_consumes_one_cycle_without_executing) {
    System system;
    prepare(system, {0x50010001, 0x0000000c, 0x24020007});
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffff80001008ULL);
    CHECK_EQ(system.cpu.cycles, 2U);
    CHECK(!system.cpu.exception_pending);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 3U);
    CHECK_EQ(system.cpu.gpr[2], 7U);
}

TEST(cpu_cached_stores_do_not_add_a_load_use_stall) {
    System system;
    prepare(system, {0xac220000, 0x24020007});
    system.cpu.gpr[2] = 0xabc;
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 2U);
    u64 value = 0;
    CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, value));
    CHECK_EQ(value, 0xabcU);
}

TEST(cpu_count_write_restarts_divider_after_the_write_hazard) {
    System system;
    prepare(system, {0x40824800, 0x40034800, 0x40044800, 0x40054800, 0x40064800});
    system.cpu.gpr[2] = 100;
    for (unsigned index = 0; index < 5; ++index)
        system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 100U);
    CHECK_EQ(system.cpu.gpr[4], 100U);
    CHECK_EQ(system.cpu.gpr[5], 100U);
    CHECK_EQ(system.cpu.gpr[6], 101U);
    CHECK_EQ(system.cpu.cycles, 6U);
}

TEST(cpu_wired_reset_reaches_random_after_one_following_instruction) {
    System system;
    prepare(system, {0x40823000, 0x40030800, 0x40040800, 0x40050800});
    system.cpu.write_cop0(6, 20);
    system.cpu.gpr[2] = 5;
    system.cpu.step();
    system.cpu.step();
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 30U);
    CHECK_EQ(system.cpu.gpr[4], 31U);
    CHECK_EQ(system.cpu.gpr[5], 30U);
}

TEST(cpu_random_wraps_at_each_of_the_64_wired_values) {
    for (u32 wired = 0; wired < 64; ++wired) {
        System system;
        prepare(system, {0x40823000, 0, 0});
        system.cpu.gpr[2] = wired;
        system.cpu.step();
        system.cpu.step();
        CHECK_EQ(system.cpu.read_cop0(1), 31U);
        u32 expected = 31;
        for (unsigned index = 0; index < 100; ++index) {
            system.cpu.step();
            expected = expected == wired ? 31U : (expected - 1U) & 63U;
            CHECK_EQ(system.cpu.read_cop0(1), expected);
        }
    }
}

TEST(cpu_software_interrupt_waits_for_the_following_instruction) {
    System system;
    prepare(system, {0x40826800, 0x24030007, 0});
    system.cpu.write_cop0(12, 0x34000101);
    system.cpu.gpr[2] = 0x100;
    system.cpu.step();
    system.cpu.step();
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.gpr[3], 7U);
    system.cpu.step();
    CHECK(system.cpu.exception_pending);
    CHECK_EQ(system.cpu.cp0[14], 0xffffffff80001008ULL);
    CHECK_EQ(system.cpu.cp0[13] & 0x7cU, 0U);
}

TEST(cpu_back_to_back_cause_writes_can_cancel_a_software_interrupt) {
    System system;
    prepare(system, {0x40826800, 0x40806800, 0, 0});
    system.cpu.write_cop0(12, 0x34000101);
    system.cpu.gpr[2] = 0x100;
    for (unsigned index = 0; index < 4; ++index) {
        system.cpu.step();
        CHECK(!system.cpu.exception_pending);
    }
    CHECK_EQ(system.cpu.cp0[13] & 0x300U, 0U);
}

TEST(cpu_consecutive_wired_writes_preserve_both_delayed_resets) {
    System system;
    prepare(system, {0x40823000, 0x40833000, 0, 0});
    system.cpu.gpr[2] = 5;
    system.cpu.gpr[3] = 10;
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.read_cop0(1), 31U);
    system.cpu.step();
    CHECK_EQ(system.cpu.read_cop0(1), 31U);
    system.cpu.step();
    CHECK_EQ(system.cpu.read_cop0(1), 30U);
}
