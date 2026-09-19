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
    system.cpu.gpr[1] = 0xffffffffa0002000ULL;
    system.cpu.gpr[2] = 0x123456789abcdef0ULL;
    system.cpu.gpr[3] = 0xffffffff80002000ULL;
    u32 address = 0x1000;
    for (const u32 instruction : instructions) {
        system.bus.write(address, 4, instruction);
        address += 4;
    }
    for (u32 line = 0x1000; line < address; line += 32) {
        u64 ignored = 0;
        CHECK(system.cpu.read_memory(0xffffffff80000000ULL | line, 4, ignored, true));
    }
}
} // namespace

TEST(cpu_uncached_store_buffer_accepts_four_stores_without_stalling) {
    for (const u32 opcode : {0x28U, 0x29U, 0x2bU, 0x3fU}) {
        System system;
        const u32 instruction = (opcode << 26) | (1U << 21) | (2U << 16);
        prepare(system, {instruction, instruction + 8, instruction + 16, instruction + 24});
        for (unsigned index = 0; index < 4; ++index)
            system.cpu.step();
        CHECK_EQ(system.cpu.cycles, 4U);
        system.advance(32);
        const unsigned width = opcode == 0x28 ? 1 : opcode == 0x29 ? 2 : opcode == 0x2b ? 4 : 8;
        const u64 mask = width == 8 ? ~0ULL : (1ULL << (width * 8)) - 1;
        for (u32 offset = 0; offset < 32; offset += 8)
            CHECK_EQ(system.bus.read(0x2000 + offset, width), system.cpu.gpr[2] & mask);
    }
}

TEST(cpu_uncached_store_is_not_visible_until_the_bus_transfer_finishes) {
    System system;
    prepare(system, {0xac220000, 0, 0, 0});
    system.cpu.step();
    CHECK_EQ(system.bus.read(0x2000, 4), 0U);
    system.cpu.gpr[2] = 0;
    system.advance(1);
    CHECK_EQ(system.bus.read(0x2000, 4), 0U);
    system.advance(2);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x9abcdef0U);
}

TEST(cpu_uncached_load_waits_for_older_buffered_stores) {
    System system;
    prepare(system, {0xac220000, 0x8c240000});
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[4], 0xffffffff9abcdef0ULL);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x9abcdef0U);
    CHECK(system.cpu.cycles > 2);
}

TEST(cpu_full_write_buffer_stalls_without_losing_or_reordering_data) {
    System system;
    prepare(system,
            {0xfc220000, 0xfc220008, 0xfc220010, 0xfc220018, 0xfc220020, 0xfc220028, 0xfc220030, 0xfc220038});
    for (u32 index = 0; index < 8; ++index) {
        system.cpu.gpr[2] = index + 1;
        system.cpu.step();
    }
    CHECK(system.cpu.cycles > 8);
    system.advance(64);
    for (u32 index = 0; index < 8; ++index)
        CHECK_EQ(system.bus.read(0x2000 + index * 8, 8), index + 1);
}

TEST(cpu_cache_fill_waits_for_older_uncached_stores) {
    System system;
    prepare(system, {0xac220000, 0x8c640000});
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[4], 0xffffffff9abcdef0ULL);
}

TEST(cpu_cache_writeback_does_not_overtake_buffered_stores) {
    System system;
    prepare(system, {0xac220000, 0xbc790000});
    CHECK(system.cpu.write_memory(system.cpu.gpr[3], 4, 0x76543210));
    system.cpu.step();
    system.cpu.step();
    system.advance(32);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x76543210U);
}

TEST(cpu_reset_discards_untransmitted_store_buffer_entries) {
    System system;
    prepare(system, {0xac220000});
    system.cpu.step();
    system.cpu.reset();
    system.advance(32);
    CHECK_EQ(system.bus.read(0x2000, 4), 0U);
}

TEST(cpu_device_register_writes_follow_the_store_buffer_clock) {
    System system;
    prepare(system, {0xac220000});
    system.cpu.gpr[1] = 0xffffffffa430000cULL;
    system.cpu.gpr[2] = 0x80;
    system.cpu.step();
    CHECK_EQ(system.bus.read(0x0430000c, 4), 0U);
    system.advance(3);
    CHECK_EQ(system.bus.read(0x0430000c, 4), 8U);
}

TEST(cpu_data_index_load_tag_has_a_six_cycle_execution_time) {
    System system;
    prepare(system, {0xbc650000});
    CHECK(system.cpu.write_memory(system.cpu.gpr[3], 4, 0));
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 6U);
    CHECK_EQ(system.cpu.cp0[28], 0x2c0U);
}

TEST(cpu_instruction_cache_fill_waits_for_buffered_stores) {
    System system;
    prepare(system, {0xac220000, 0xbc740000});
    system.cpu.step();
    system.cpu.step();
    u64 instruction = 0;
    CHECK(system.cpu.read_memory(system.cpu.gpr[3], 4, instruction, true));
    CHECK_EQ(instruction, 0x9abcdef0U);
}

TEST(cpu_instruction_cache_writeback_does_not_overtake_buffered_stores) {
    System system;
    prepare(system, {0xac220000, 0xbc780000});
    system.bus.write(0x2000, 4, 0x76543210);
    u64 instruction = 0;
    CHECK(system.cpu.read_memory(system.cpu.gpr[3], 4, instruction, true));
    system.cpu.step();
    system.cpu.step();
    system.advance(32);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x76543210U);
}

TEST(cpu_write_buffer_respects_the_slow_block_data_pattern) {
    for (const u32 pattern : {0U, 6U}) {
        System system;
        prepare(system, {0xfc220000});
        system.cpu.write_cop0(16, (system.cpu.cp0[16] & ~0x0f000000ULL) | (pattern << 24));
        system.cpu.step();
        system.advance(4);
        CHECK_EQ(system.bus.read(0x2000, 8), pattern == 0 ? 0x123456789abcdef0ULL : 0ULL);
        system.advance(3);
        CHECK_EQ(system.bus.read(0x2000, 8), 0x123456789abcdef0ULL);
    }
}

TEST(cpu_write_buffer_does_not_change_sync_into_a_bus_barrier) {
    System system;
    prepare(system, {0xfc220000, 0x0000000f});
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 2U);
    CHECK_EQ(system.bus.read(0x2000, 8), 0U);
    system.advance(4);
    CHECK_EQ(system.bus.read(0x2000, 8), 0x123456789abcdef0ULL);
}

TEST(cpu_buffered_device_writes_retire_in_fifo_order) {
    System system;
    prepare(system, {0xac220000, 0xac240000});
    system.cpu.gpr[1] = 0xffffffffa430000cULL;
    system.cpu.gpr[2] = 0x80;
    system.cpu.gpr[4] = 0x40;
    system.cpu.step();
    system.cpu.step();
    system.advance(2);
    CHECK_EQ(system.bus.read(0x0430000c, 4), 8U);
    // The next two external-clock edges fall at CPU cycles 5 and 6.
    system.advance(1);
    CHECK_EQ(system.bus.read(0x0430000c, 4), 8U);
    system.advance(1);
    CHECK_EQ(system.bus.read(0x0430000c, 4), 0U);
}

TEST(cpu_cached_hit_does_not_drain_or_snoop_the_write_buffer) {
    System system;
    prepare(system, {0xac220000, 0x8c640000});
    system.bus.write(0x2000, 4, 0x76543210);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.gpr[3], 4, ignored));
    system.cpu.step();
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 2U);
    CHECK_EQ(system.cpu.gpr[4], 0x76543210U);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x76543210U);
    system.advance(2);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x9abcdef0U);
}

TEST(cpu_partial_stores_use_one_buffer_entry_per_instruction) {
    for (const u32 instruction : {0xa8220001U, 0xb8220002U, 0xb0220001U, 0xb4220006U}) {
        System system;
        prepare(system, {instruction, instruction + 8, instruction + 16, instruction + 24});
        for (unsigned index = 0; index < 4; ++index)
            system.cpu.step();
        CHECK_EQ(system.cpu.cycles, 4U);
        system.advance(64);
        const u64 expected = instruction == 0xa8220001U   ? 0x009abcde00000000ULL
                             : instruction == 0xb8220002U ? 0xbcdef00000000000ULL
                             : instruction == 0xb0220001U ? 0x00123456789abcdeULL
                                                          : 0x3456789abcdef000ULL;
        for (u32 offset = 0; offset < 32; offset += 8)
            CHECK_EQ(system.bus.read(0x2000 + offset, 8), expected);
    }
}
