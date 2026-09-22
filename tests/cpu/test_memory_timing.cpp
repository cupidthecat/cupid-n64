#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

void prepare(System& system, u64 address) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.gpr[1] = address;
    system.bus.write(0x1000, 4, 0x8c220000); // LW v0, 0(at)
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
}
} // namespace

TEST(cpu_rdram_uncached_word_reads_include_the_memory_response_delay) {
    for (u32 bank = 0; bank < 8; ++bank) {
        System system;
        const u32 physical = bank * 0x100000U + 0x2000;
        prepare(system, 0xffffffffa0000000ULL | physical);
        system.bus.write(physical, 4, 0x12345678);
        system.cpu.step();
        CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
        CHECK_EQ(system.cpu.cycles, 32U);
        CHECK_EQ(system.cpu.cp0[9], 16U);
        CHECK_EQ(system.bus.read(0x04100010, 4), 21U);
    }
}

TEST(cpu_rdram_read_delay_does_not_apply_to_device_registers) {
    System system;
    prepare(system, 0xffffffffa4300004ULL);
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[2], 0x02020102U);
    CHECK_EQ(system.cpu.cycles, 5U);
}

TEST(cpu_rdram_cached_reads_keep_the_cache_hit_and_refill_paths) {
    for (bool hit : {false, true}) {
        System system;
        prepare(system, 0xffffffff80002000ULL);
        system.bus.write(0x2000, 4, 0x12345678);
        if (hit) {
            u64 ignored = 0;
            CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, ignored));
        }
        system.cpu.step();
        CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
        CHECK_EQ(system.cpu.cycles, hit ? 1U : 42U);
    }
}

TEST(cpu_rdram_read_address_fault_precedes_the_memory_request) {
    System system;
    prepare(system, 0xffffffffa0002001ULL);
    system.cpu.step();
    CHECK(system.cpu.exception_pending);
    CHECK_EQ(system.cpu.cp0[8], 0xffffffffa0002001ULL);
    CHECK_EQ(system.cpu.cycles, 5U);
}

TEST(cpu_rdram_response_delay_uses_the_translated_address_and_cache_attribute) {
    System system;
    prepare(system, 0x4000);
    auto& entry = system.cpu.tlb[0];
    entry.entry_hi = 0x4000;
    entry.entry_lo[0] = (2U << 6) | (2U << 3) | 7U;
    entry.global = true;
    system.bus.write(0x2000, 4, 0x12345678);
    system.cpu.step();
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
    CHECK_EQ(system.cpu.cycles, 32U);
    CHECK(!system.cpu.data_cache[0].valid);
}

TEST(cpu_rdram_response_wait_advances_compare_without_interrupting_the_load) {
    System system;
    prepare(system, 0xffffffffa0002000ULL);
    system.cpu.write_cop0(11, 4);
    system.bus.write(0x2000, 4, 0x12345678);
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0x8000U);
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
    CHECK_EQ(system.cpu.cycles, 32U);
}
