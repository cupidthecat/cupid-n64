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
