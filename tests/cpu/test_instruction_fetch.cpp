#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u32 original = 0x34031111; // ORI v1, zero, 0x1111
constexpr u32 replacement = 0x34032222;

void prepare(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(code);
}
} // namespace

TEST(cpu_instruction_fetch_keeps_the_next_cache_hit_across_invalidation) {
    System system;
    prepare(system);
    system.cpu.gpr[1] = code;
    system.bus.write(0x1000, 4, 0xbc200000); // CACHE IndexInvalidateI, 0(at)
    system.bus.write(0x1004, 4, original);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code, 4, ignored, true));
    system.bus.write(0x1004, 4, replacement);
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 0x1111U);
    system.cpu.set_pc(code + 4);
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 0x2222U);
}

TEST(cpu_instruction_fetch_precedes_the_previous_instruction_data_writeback) {
    for (u32 writeback : {0xbc390000U, 0xac242000U}) {
        System system;
        prepare(system);
        system.cpu.set_pc(code + 28);
        system.cpu.gpr[1] = code + 32;
        system.bus.write(0x101c, 4, writeback);
        system.bus.write(0x1020, 4, original);
        CHECK(system.cpu.write_memory(code + 32, 4, replacement));
        system.cpu.step();
        system.cpu.step();
        CHECK_EQ(system.cpu.gpr[3], 0x1111U);
        CHECK_EQ(system.bus.read(0x1020, 4), replacement);
    }
}

TEST(cpu_instruction_fetch_retries_a_miss_invalidated_by_the_older_instruction) {
    System system;
    prepare(system);
    system.cpu.set_pc(code + 28);
    system.cpu.gpr[1] = code + 32;
    system.bus.write(0x101c, 4, 0xbc200000);
    system.bus.write(0x1020, 4, replacement);
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 0x2222U);
}

TEST(cpu_instruction_fetch_reaches_two_ahead_before_an_uncached_store_is_issued) {
    for (u32 store_index : {5U, 6U, 7U}) {
        System system;
        prepare(system);
        system.cpu.gpr[1] = 0xffffffffa0001020ULL;
        system.cpu.gpr[2] = replacement;
        system.bus.write(0x1000 + store_index * 4, 4, 0xac220000);
        system.bus.write(0x1020, 4, original);
        for (unsigned instruction = 0; instruction <= 8; ++instruction)
            system.cpu.step();
        CHECK_EQ(system.cpu.gpr[3], store_index == 5 ? 0x2222U : 0x1111U);
        system.advance(8);
        CHECK_EQ(system.bus.read(0x1020, 4), replacement);
    }
}

TEST(cpu_instruction_fetch_orders_consecutive_stores_before_the_line_refill) {
    System system;
    prepare(system);
    system.cpu.gpr[1] = 0xffffffffa0001020ULL;
    for (u32 index = 0; index < 8; ++index) {
        system.cpu.gpr[16 + index] = 0x34630000U | (1U << index); // ORI v1, v1, mask
        system.bus.write(0x1000 + index * 4, 4, 0xac200000U | ((16 + index) << 16) | (index * 4));
    }
    for (unsigned instruction = 0; instruction < 16; ++instruction)
        system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 0x3fU);
}

TEST(cpu_instruction_fetch_does_not_validate_a_failed_speculative_refill) {
    System system;
    prepare(system);
    system.cpu.gpr[1] = 0xffffffff84001000ULL;
    system.bus.write(0x1000, 4, 0x00200008); // JR at
    system.bus.write(0x1004, 4, 0x34030042);
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 0x42U);
    CHECK(!system.cpu.frozen);
    system.cpu.step();
    CHECK(system.cpu.frozen);
}

TEST(cpu_instruction_fetch_miss_waits_after_the_older_device_read) {
    System system;
    prepare(system);
    system.cpu.set_pc(code + 24);
    system.cpu.gpr[1] = 0xffffffffa4100010ULL;
    system.bus.write(0x1018, 4, 0x40024800); // MFC0 v0, Count
    system.bus.write(0x101c, 4, 0x8c230000); // LW v1, DPC_CLOCK(at)
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code + 24, 4, ignored, true));
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[3], 4U);
    CHECK_EQ(system.cpu.cycles, 54U);
    CHECK_EQ(system.bus.read(0x04100010, 4), 36U);
}

TEST(cpu_instruction_fetch_defers_a_target_fault_until_after_the_delay_slot) {
    System system;
    prepare(system);
    system.cpu.gpr[1] = 0x4000;
    system.cpu.cp0[8] = 0x12345678;
    system.bus.write(0x1000, 4, 0x00200008);
    system.bus.write(0x1004, 4, 0x34030042);
    system.cpu.step();
    system.cpu.step();
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.gpr[3], 0x42U);
    CHECK_EQ(system.cpu.cp0[8], 0x12345678U);
    system.cpu.step();
    CHECK(system.cpu.exception_pending);
    CHECK_EQ(system.cpu.cp0[8], 0x4000U);
    CHECK_EQ(system.cpu.cp0[14], 0x4000U);
    CHECK_EQ(system.cpu.cp0[13] & 0x8000007cU, 8U);
}

TEST(cpu_instruction_fetch_discards_an_annulled_delay_slot) {
    System system;
    prepare(system);
    system.cpu.gpr[1] = 1;
    system.bus.write(0x1000, 4, 0x50010003); // BEQL zero, at, 0x1010
    system.bus.write(0x1004, 4, 0x0000000d); // BREAK in the annulled slot
    system.bus.write(0x1008, 4, replacement);
    system.cpu.step();
    system.cpu.step();
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.gpr[3], 0x2222U);
    CHECK_EQ(system.cpu.instruction_count, 2U);
}
