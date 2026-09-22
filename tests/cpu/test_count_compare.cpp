#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

void prepare(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    // Read Count, set a future Compare value, and poll Cause with a dependent shift.
    constexpr u32 code[] = {0x40064800, 0x00c53021, 0x00c73021, 0x40865800, 0,          0, 0x3c030000,
                            0x40046800, 0x000423c2, 0x30840001, 0x5080fffc, 0x24630001, 0};
    for (unsigned index = 0; index < std::size(code); ++index)
        system.bus.write(0x1000 + index * 4, 4, code[index]);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(0xffffffff80001000ULL, 4, ignored, true));
    CHECK(system.cpu.read_memory(0xffffffff80001020ULL, 4, ignored, true));
    system.cpu.set_pc(0xffffffff80001000ULL);
}
} // namespace

TEST(cpu_compare_polling_observes_the_count_sampling_phase) {
    for (unsigned phase = 0; phase < 2; ++phase) {
        for (u32 offset : {4U, 50U, 100U, 500U, 2000U}) {
            System system;
            prepare(system);
            if (phase != 0) {
                system.cpu.set_pc(0xffffffff80001030ULL);
                system.cpu.step();
                system.cpu.set_pc(0xffffffff80001000ULL);
            }
            system.cpu.gpr[5] = offset;
            for (unsigned steps = 0; steps < 10000 && system.cpu.pc != 0xffffffff80001030ULL; ++steps)
                system.cpu.step();
            CHECK_EQ(system.cpu.pc, 0xffffffff80001030ULL);
            CHECK_EQ(system.cpu.gpr[3], (offset - 2) / 3);
        }
    }
}

TEST(cpu_count_reads_include_the_issue_cycle_without_advancing_clocks_twice) {
    for (u32 opcode : {0x40034800U, 0x40234800U}) {
        System system;
        prepare(system);
        system.bus.write(0x1000, 4, 0);
        system.bus.write(0x1004, 4, opcode);
        system.cpu.cache_operation(0x00, 0xffffffff80001000ULL);
        u64 ignored = 0;
        CHECK(system.cpu.read_memory(0xffffffff80001000ULL, 4, ignored, true));
        system.cpu.write_cop0(9, 0xffffffffU);
        system.cpu.step();
        CHECK_EQ(system.cpu.read_cop0(9), 0xffffffffU);
        system.cpu.step();
        CHECK_EQ(system.cpu.gpr[3], 0U);
        CHECK_EQ(system.cpu.read_cop0(9), 0U);
        CHECK_EQ(system.cpu.cycles, 2U);
    }
}

TEST(cpu_compare_match_at_count_wrap_remains_pending_until_acknowledged) {
    System system;
    prepare(system);
    system.cpu.set_pc(0xffffffff80001030ULL);
    system.cpu.write_cop0(9, 0xffffffffU);
    system.cpu.write_cop0(11, 0);
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0U);
    system.cpu.step();
    CHECK_EQ(system.cpu.read_cop0(9), 0U);
    CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0x8000U);
    system.cpu.write_cop0(9, 10);
    CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0x8000U);
    system.cpu.write_cop0(11, 20);
    CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0U);
}
