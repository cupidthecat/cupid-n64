#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {

using namespace cupid;

constexpr u32 special(unsigned function, unsigned rs, unsigned rt, unsigned rd = 0, unsigned shift = 0) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shift << 6) | function;
}

constexpr u32 immediate(unsigned opcode, unsigned rs, unsigned rt, u16 value) {
    return (opcode << 26) | (rs << 21) | (rt << 16) | value;
}

std::unique_ptr<System> machine() {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    system->cpu.write_cop0(12, 0x34000000U);
    system->cpu.set_pc(0xffffffffa0001000ULL);
    return system;
}

void instruction(System& system, u32 address, u32 value) {
    system.bus.write(address, 4, value);
}

void map(Cpu& cpu, unsigned index, u64 virtual_address, u32 physical0, u32 physical1, u32 mask = 0,
         u32 flags0 = 0x17, u32 flags1 = 0x17) {
    cpu.write_cop0(2, (physical0 >> 6) | flags0);
    cpu.write_cop0(3, (physical1 >> 6) | flags1);
    cpu.write_cop0(5, mask);
    cpu.write_cop0(10, virtual_address);
    cpu.tlb_write(index);
}

u32 exception_code(const Cpu& cpu) {
    return static_cast<u32>((cpu.cp0[13] >> 2) & 31U);
}

} // namespace

TEST(cpu_word_arithmetic_sign_extends_and_preserves_zero) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0x123456787fffffffULL;
    cpu.gpr[2] = 1;
    cpu.execute(special(0x21, 1, 2, 3));
    CHECK_EQ(cpu.gpr[3], 0xffffffff80000000ULL);
    cpu.execute(special(0x21, 1, 2, 0));
    CHECK_EQ(cpu.gpr[0], 0ULL);
    cpu.execute(immediate(0x0f, 0, 4, 0x89ab));
    CHECK_EQ(cpu.gpr[4], 0xffffffff89ab0000ULL);
    cpu.execute(immediate(0x0b, 2, 5, 0xffff));
    CHECK_EQ(cpu.gpr[5], 1ULL);
}

TEST(cpu_overflow_to_zero_still_raises_and_keeps_destination) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0x7fffffffU;
    cpu.gpr[2] = 1;
    cpu.gpr[3] = 0xfeed;
    cpu.execute(special(0x20, 1, 2, 3));
    CHECK(cpu.exception_pending);
    CHECK_EQ(exception_code(cpu), 12U);
    CHECK_EQ(cpu.gpr[3], 0xfeedULL);
    CHECK_EQ(cpu.cp0[14], 0xffffffffa0001000ULL);
    CHECK_EQ(cpu.pc, 0xffffffff80000180ULL);
    cpu.reset();
    cpu.write_cop0(12, 0);
    cpu.gpr[1] = 0x7fffffffffffffffULL;
    cpu.gpr[2] = 1;
    cpu.execute(special(0x2c, 1, 2, 0));
    CHECK(cpu.exception_pending);
    CHECK_EQ(exception_code(cpu), 12U);
    CHECK_EQ(cpu.gpr[0], 0ULL);
}

TEST(cpu_division_extremes_are_defined) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0x8000000000000000ULL;
    cpu.gpr[2] = ~0ULL;
    cpu.execute(special(0x1e, 1, 2));
    CHECK_EQ(cpu.lo, 0x8000000000000000ULL);
    CHECK_EQ(cpu.hi, 0ULL);
    cpu.gpr[2] = 0;
    cpu.execute(special(0x1e, 1, 2));
    CHECK_EQ(cpu.lo, 1ULL);
    CHECK_EQ(cpu.hi, cpu.gpr[1]);
    cpu.gpr[1] = 0xffffffff80000000ULL;
    cpu.gpr[2] = ~0ULL;
    cpu.execute(special(0x1a, 1, 2));
    CHECK_EQ(cpu.lo, 0xffffffff80000000ULL);
    CHECK_EQ(cpu.hi, 0ULL);
    cpu.gpr[2] = 0;
    cpu.execute(special(0x1b, 1, 2));
    CHECK_EQ(cpu.lo, ~0ULL);
    CHECK_EQ(cpu.hi, 0xffffffff80000000ULL);
}

TEST(cpu_doubleword_multiply_retains_both_halves) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = ~0ULL;
    cpu.gpr[2] = ~0ULL;
    cpu.execute(special(0x1d, 1, 2));
    CHECK_EQ(cpu.lo, 1ULL);
    CHECK_EQ(cpu.hi, 0xfffffffffffffffeULL);
    cpu.execute(special(0x1c, 1, 2));
    CHECK_EQ(cpu.lo, 1ULL);
    CHECK_EQ(cpu.hi, 0ULL);
    cpu.gpr[1] = 0x8000000000000000ULL;
    cpu.gpr[2] = 2;
    cpu.execute(special(0x1c, 1, 2));
    CHECK_EQ(cpu.lo, 0ULL);
    CHECK_EQ(cpu.hi, ~0ULL);
    cpu.gpr[1] = 0x0123456789abcdefULL;
    cpu.gpr[2] = 0xfedcba9876543210ULL;
    cpu.execute(special(0x1d, 1, 2));
    CHECK_EQ(cpu.lo, 0x2236d88fe5618cf0ULL);
    CHECK_EQ(cpu.hi, 0x0121fa00ad77d742ULL);
}

TEST(cpu_word_shift_uses_full_input_for_arithmetic_shift) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0x0000000180000000ULL;
    cpu.execute(special(0x03, 0, 1, 2, 1));
    CHECK_EQ(cpu.gpr[2], 0xffffffffc0000000ULL);
    cpu.execute(special(0x02, 0, 1, 2, 1));
    CHECK_EQ(cpu.gpr[2], 0x40000000ULL);
    cpu.gpr[3] = 65;
    cpu.execute(special(0x14, 3, 1, 2));
    CHECK_EQ(cpu.gpr[2], 0x0000000300000000ULL);
}

TEST(cpu_taken_branch_executes_exactly_one_delay_slot) {
    auto system = machine();
    instruction(*system, 0x1000, immediate(4, 0, 0, 3));
    instruction(*system, 0x1004, immediate(9, 0, 1, 11));
    instruction(*system, 0x1008, immediate(9, 0, 1, 22));
    instruction(*system, 0x1010, immediate(9, 0, 2, 33));
    system->cpu.step();
    CHECK_EQ(system->cpu.pc, 0xffffffffa0001004ULL);
    system->cpu.step();
    CHECK_EQ(system->cpu.pc, 0xffffffffa0001010ULL);
    system->cpu.step();
    CHECK_EQ(system->cpu.gpr[1], 11ULL);
    CHECK_EQ(system->cpu.gpr[2], 33ULL);
}

TEST(cpu_not_taken_branch_marks_exception_as_delay_slot) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 1;
    instruction(*system, 0x1000, immediate(4, 0, 1, 4));
    instruction(*system, 0x1004, 0x0000000cU);
    cpu.step();
    cpu.step();
    CHECK_EQ(exception_code(cpu), 8U);
    CHECK_EQ(cpu.cp0[14], 0xffffffffa0001000ULL);
    CHECK((cpu.cp0[13] & 0x80000000U) != 0);
}

TEST(cpu_likely_annuls_faulting_delay_slot) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 1;
    instruction(*system, 0x1000, immediate(0x14, 0, 1, 4));
    instruction(*system, 0x1004, 0x0000000cU);
    instruction(*system, 0x1008, immediate(9, 0, 2, 7));
    cpu.step();
    CHECK_EQ(cpu.pc, 0xffffffffa0001008ULL);
    cpu.step();
    CHECK(!cpu.exception_pending);
    CHECK_EQ(cpu.gpr[2], 7ULL);
}

TEST(cpu_nested_branch_uses_pending_target_as_base) {
    auto system = machine();
    instruction(*system, 0x1000, immediate(4, 0, 0, 3));
    instruction(*system, 0x1004, immediate(4, 0, 0, 2));
    instruction(*system, 0x1010, immediate(9, 0, 1, 5));
    instruction(*system, 0x1018, immediate(9, 0, 2, 6));
    auto& cpu = system->cpu;
    cpu.step();
    cpu.step();
    CHECK_EQ(cpu.pc, 0xffffffffa0001010ULL);
    cpu.step();
    CHECK_EQ(cpu.pc, 0xffffffffa0001018ULL);
    cpu.step();
    CHECK_EQ(cpu.gpr[1], 5ULL);
    CHECK_EQ(cpu.gpr[2], 6ULL);
}

TEST(cpu_jalr_reads_target_before_writing_link) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[5] = 0xffffffffa0002000ULL;
    instruction(*system, 0x1000, special(9, 5, 0, 5));
    instruction(*system, 0x1004, 0);
    cpu.step();
    cpu.step();
    CHECK_EQ(cpu.pc, 0xffffffffa0002000ULL);
    CHECK_EQ(cpu.gpr[5], 0xffffffffa0001008ULL);
}

TEST(cpu_nested_exception_preserves_epc_and_delay_flag) {
    auto system = machine();
    auto& cpu = system->cpu;
    instruction(*system, 0x1000, immediate(4, 0, 0, 3));
    instruction(*system, 0x1004, 0x0000000cU);
    cpu.step();
    cpu.step();
    const u64 epc = cpu.cp0[14];
    cpu.raise_exception(Exception::Breakpoint);
    CHECK_EQ(cpu.cp0[14], epc);
    CHECK((cpu.cp0[13] & 0x80000000U) != 0);
    CHECK_EQ(exception_code(cpu), 9U);
}

TEST(cpu_alignment_fault_precedes_tlb_and_updates_context) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(4, 0x1234567800000000ULL);
    cpu.write_cop0(10, 0x55);
    u64 value = 0xdead;
    CHECK(!cpu.read_memory(0x01234567, 4, value));
    CHECK_EQ(exception_code(cpu), 4U);
    CHECK_EQ(value, 0xdeadULL);
    CHECK_EQ(cpu.cp0[8], 0x01234567ULL);
    CHECK_EQ(cpu.cp0[10], 0x01234055ULL);
    CHECK_EQ(cpu.cp0[4], 0x12345678000091a0ULL);
}

TEST(cpu_32bit_addressing_rejects_zero_extended_kernel_address) {
    auto system = machine();
    u64 value = 0;
    CHECK(!system->cpu.read_memory(0x0000000080000000ULL, 4, value));
    CHECK_EQ(exception_code(system->cpu), 4U);
    CHECK_EQ(system->cpu.cp0[8], 0x0000000080000000ULL);
}

TEST(cpu_tlb_asid_global_and_even_odd_page_selection) {
    auto system = machine();
    auto& cpu = system->cpu;
    map(cpu, 3, 0x00400077, 0x2000, 0x5000, 0, 0x16, 0x16);
    system->bus.write(0x2000, 4, 0x12345678);
    system->bus.write(0x5000, 4, 0xabcdef01);
    cpu.write_cop0(10, 0x77);
    u64 value = 0;
    CHECK(cpu.read_memory(0x00400000, 4, value));
    CHECK_EQ(value, 0x12345678ULL);
    CHECK(cpu.read_memory(0x00401000, 4, value));
    CHECK_EQ(value, 0xabcdef01ULL);
    cpu.write_cop0(10, 0x78);
    CHECK(!cpu.read_memory(0x00400000, 4, value));
    CHECK_EQ(exception_code(cpu), 2U);
    CHECK_EQ(cpu.pc, 0xffffffff80000000ULL);
    cpu.write_cop0(12, 0x34000000);
    cpu.exception_pending = false;
    map(cpu, 3, 0x00400077, 0x2000, 0x5000);
    cpu.write_cop0(10, 0x99);
    CHECK(cpu.read_memory(0x00401000, 4, value));
    CHECK_EQ(value, 0xabcdef01ULL);
}

TEST(cpu_tlb_dirty_fault_does_not_write_memory) {
    auto system = machine();
    auto& cpu = system->cpu;
    map(cpu, 0, 0x00400000, 0x2000, 0x3000, 0, 0x13, 0x13);
    system->bus.write(0x2000, 4, 0x89abcdef);
    CHECK(!cpu.write_memory(0x00400000, 4, 0x12345678));
    CHECK_EQ(exception_code(cpu), 1U);
    CHECK_EQ(system->bus.read(0x2000, 4), 0x89abcdefULL);
    CHECK_EQ(cpu.pc, 0xffffffff80000180ULL);
}

TEST(cpu_tlb_normalizes_page_masks_and_global_bits) {
    auto system = machine();
    auto& cpu = system->cpu;
    map(cpu, 2, 0x12346045, 0x8000, 0x18000, 0x2000, 0x17, 0x16);
    cpu.write_cop0(0, 2);
    cpu.tlb_read();
    CHECK_EQ(cpu.cp0[5], 0ULL);
    CHECK_EQ(cpu.cp0[2] & 1U, 0ULL);
    CHECK_EQ(cpu.cp0[3] & 1U, 0ULL);
    map(cpu, 2, 0x12346045, 0x8000, 0x18000, 0x4000);
    cpu.tlb_read();
    CHECK_EQ(cpu.cp0[5], 0x6000ULL);
    CHECK_EQ(cpu.cp0[10], 0x12340045ULL);
}

TEST(cpu_tlb_probe_ignores_valid_bits_but_matches_asid) {
    auto system = machine();
    auto& cpu = system->cpu;
    map(cpu, 7, 0x02000033, 0, 0, 0, 0, 0);
    cpu.write_cop0(10, 0x02000033);
    cpu.tlb_probe();
    CHECK_EQ(cpu.cp0[0], 7ULL);
    cpu.write_cop0(10, 0x02000034);
    cpu.tlb_probe();
    CHECK_EQ(cpu.cp0[0], 0x80000000ULL);
}

TEST(cpu_large_tlb_page_keeps_physical_page_offset_bits) {
    auto system = machine();
    auto& cpu = system->cpu;
    map(cpu, 1, 0x00400000, 0x1000, 0x9000, 0x6000);
    u32 physical = 0;
    bool cached = true;
    CHECK(cpu.translate(0x00403000, Access::Read, physical, cached));
    CHECK_EQ(physical, 0x4000U);
    CHECK(!cached);
    CHECK(cpu.translate(0x00404000, Access::Read, physical, cached));
    CHECK_EQ(physical, 0x9000U);
}

TEST(cpu_extended_segments_respect_privilege_and_physical_width) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(12, 0x34000080);
    u32 physical = 0;
    bool cached = true;
    CHECK(cpu.translate(0x9000000012345678ULL, Access::Read, physical, cached));
    CHECK_EQ(physical, 0x12345678U);
    CHECK(!cached);
    CHECK(!cpu.translate(0x9000000100000000ULL, Access::Read, physical, cached));
    CHECK_EQ(exception_code(cpu), 4U);
    cpu.write_cop0(12, 0x34000030);
    CHECK(!cpu.translate(0xffffffff80000000ULL, Access::Read, physical, cached));
    CHECK_EQ(exception_code(cpu), 4U);
}

TEST(cpu_user_64bit_instruction_gate_uses_ux) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(12, 0x34000010);
    cpu.gpr[1] = 1;
    cpu.gpr[2] = 2;
    cpu.gpr[3] = 7;
    cpu.execute(special(0x2d, 1, 2, 3));
    CHECK_EQ(exception_code(cpu), 10U);
    CHECK_EQ(cpu.gpr[3], 7ULL);
    cpu.write_cop0(12, 0x34000030);
    cpu.exception_pending = false;
    cpu.execute(special(0x2d, 1, 2, 3));
    CHECK(!cpu.exception_pending);
    CHECK_EQ(cpu.gpr[3], 3ULL);
}

TEST(cpu_data_cache_is_not_coherent_with_uncached_memory) {
    auto system = machine();
    auto& cpu = system->cpu;
    system->bus.write(0x4000, 4, 0x11112222);
    u64 value = 0;
    CHECK(cpu.read_memory(0xffffffff80004000ULL, 4, value));
    CHECK_EQ(value, 0x11112222ULL);
    CHECK(cpu.write_memory(0xffffffffa0004000ULL, 4, 0x33334444));
    CHECK(cpu.read_memory(0xffffffff80004000ULL, 4, value));
    CHECK_EQ(value, 0x11112222ULL);
    CHECK(cpu.write_memory(0xffffffff80004000ULL, 4, 0x55556666));
    CHECK_EQ(system->bus.read(0x4000, 4), 0x33334444ULL);
    cpu.cache_operation(0x19, 0xffffffff80004000ULL);
    CHECK_EQ(system->bus.read(0x4000, 4), 0x55556666ULL);
    CHECK(!cpu.data_cache[0].dirty);
}

TEST(cpu_cache_invalidation_does_not_write_dirty_data) {
    auto system = machine();
    auto& cpu = system->cpu;
    system->bus.write(0x4000, 4, 0x10203040);
    CHECK(cpu.write_memory(0xffffffff80004000ULL, 4, 0x50607080));
    cpu.cache_operation(0x11, 0xffffffff80004000ULL);
    CHECK_EQ(system->bus.read(0x4000, 4), 0x10203040ULL);
    u64 value = 0;
    CHECK(cpu.read_memory(0xffffffff80004000ULL, 4, value));
    CHECK_EQ(value, 0x10203040ULL);
}

TEST(cpu_instruction_cache_requires_explicit_invalidation) {
    auto system = machine();
    auto& cpu = system->cpu;
    instruction(*system, 0x1000, immediate(9, 0, 1, 1));
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.step();
    CHECK_EQ(cpu.gpr[1], 1ULL);
    instruction(*system, 0x1000, immediate(9, 0, 1, 2));
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.step();
    CHECK_EQ(cpu.gpr[1], 1ULL);
    cpu.cache_operation(0x10, 0xffffffff80001000ULL);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.step();
    CHECK_EQ(cpu.gpr[1], 2ULL);
}

TEST(cpu_cache_tag_load_reports_state_and_physical_tag) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(28, 0x00123480);
    cpu.cache_operation(0x09, 0xffffffff80000000ULL);
    cpu.cache_operation(0x05, 0xffffffff80000000ULL);
    CHECK_EQ(cpu.cp0[28], 0x001234c0ULL);
    CHECK(!cpu.data_cache[0].dirty);
    cpu.cache_operation(0x08, 0xffffffff80000000ULL);
    cpu.cache_operation(0x04, 0xffffffff80000000ULL);
    CHECK_EQ(cpu.cp0[28], 0x00123480ULL);
}

TEST(cpu_ll_sc_tracks_physical_line_and_eret_clears_only_link) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0xffffffffa0004000ULL;
    system->bus.write(0x4000, 4, 0x89abcdef);
    cpu.execute(immediate(0x30, 1, 2, 0));
    CHECK_EQ(cpu.gpr[2], 0xffffffff89abcdefULL);
    CHECK_EQ(cpu.cp0[17], 0x400ULL);
    cpu.gpr[2] = 0x76543210;
    cpu.execute(immediate(0x38, 1, 2, 0x40));
    CHECK_EQ(cpu.gpr[2], 1ULL);
    CHECK_EQ(system->bus.read(0x4040, 4), 0x76543210ULL);
    cpu.write_cop0(12, 0x34000002);
    cpu.write_cop0(14, 0xffffffffa0002000ULL);
    cpu.execute(0x42000018);
    CHECK(!cpu.linked);
    CHECK_EQ(cpu.pc, 0xffffffffa0002000ULL);
    CHECK_EQ(cpu.cp0[17], 0x400ULL);
    cpu.gpr[2] = 0x55555555;
    cpu.execute(immediate(0x38, 1, 2, 0));
    CHECK_EQ(cpu.gpr[2], 0ULL);
    CHECK_EQ(system->bus.read(0x4000, 4), 0x89abcdefULL);
}

TEST(cpu_partial_word_loads_have_distinct_upper_half_behavior) {
    auto system = machine();
    auto& cpu = system->cpu;
    system->bus.write(0x4000, 4, 0x89abcdef);
    cpu.gpr[1] = 0xffffffffa0004000ULL;
    cpu.gpr[2] = 0x1234567876543210ULL;
    cpu.execute(immediate(0x26, 1, 2, 0));
    CHECK_EQ(cpu.gpr[2], 0x1234567876543289ULL);
    cpu.execute(immediate(0x26, 1, 2, 3));
    CHECK_EQ(cpu.gpr[2], 0xffffffff89abcdefULL);
    cpu.gpr[2] = 0x1234567876543210ULL;
    cpu.execute(immediate(0x22, 1, 2, 1));
    CHECK_EQ(cpu.gpr[2], 0xffffffffabcdef10ULL);
}

TEST(cpu_partial_doubleword_stores_preserve_disabled_bytes) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0xffffffffa0004000ULL;
    cpu.gpr[2] = 0x0123456789abcdefULL;
    system->bus.write(0x4000, 8, ~0ULL);
    cpu.execute(immediate(0x2c, 1, 2, 3));
    CHECK_EQ(system->bus.read(0x4000, 8), 0xffffff0123456789ULL);
    system->bus.write(0x4000, 8, ~0ULL);
    cpu.execute(immediate(0x2d, 1, 2, 4));
    CHECK_EQ(system->bus.read(0x4000, 8), 0x6789abcdefffffffULL);
}

TEST(cpu_coprocessor_unusable_precedes_memory_alignment) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(12, 0x10000000);
    cpu.gpr[1] = 0xffffffffa0004001ULL;
    cpu.execute(immediate(0x31, 1, 2, 0));
    CHECK_EQ(exception_code(cpu), 11U);
    CHECK_EQ((cpu.cp0[13] >> 28) & 3U, 1ULL);
    CHECK_EQ(cpu.cp0[8], 0ULL);
}

TEST(cpu_cop2_uses_one_latch_for_every_register) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(12, 0x74000000);
    cpu.gpr[1] = 0x1234567889abcdefULL;
    cpu.execute((0x12U << 26) | (4U << 21) | (1U << 16) | (3U << 11));
    cpu.execute((0x12U << 26) | (2U << 16) | (7U << 11));
    CHECK_EQ(cpu.gpr[2], 0xffffffff89abcdefULL);
    cpu.execute((0x12U << 26) | (1U << 21) | (3U << 16) | (31U << 11));
    CHECK_EQ(cpu.gpr[3], 0x1234567889abcdefULL);
}

TEST(cpu_cop2_memory_transfers_use_the_shared_doubleword_latch) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(12, 0x74000000);
    cpu.gpr[1] = 0xffffffffa0004000ULL;
    system->bus.write(0x4000, 8, 0xfedcba9876543210ULL);

    cpu.execute(immediate(0x32, 1, 9, 4));
    cpu.execute((0x12U << 26) | (1U << 21) | (2U << 16) | (9U << 11));
    CHECK_EQ(cpu.gpr[2], 0xfedcba9876543210ULL);

    cpu.gpr[3] = 0xffffffffa0005000ULL;
    cpu.execute(immediate(0x3e, 3, 9, 0));
    cpu.execute(immediate(0x3a, 3, 9, 12));
    CHECK_EQ(system->bus.read(0x5000, 8), 0xfedcba9876543210ULL);
    CHECK_EQ(system->bus.read(0x500c, 4), 0x76543210ULL);
}

TEST(cpu_cop2_memory_transfers_check_usability_before_alignment) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(12, 0x34000000);
    cpu.gpr[1] = 0xffffffffa0004001ULL;
    cpu.execute(immediate(0x36, 1, 2, 0));
    CHECK_EQ(exception_code(cpu), 11U);
    CHECK_EQ((cpu.cp0[13] >> 28) & 3U, 2ULL);
    CHECK_EQ(cpu.cp0[8], 0ULL);
}

TEST(cpu_cop0_masks_registers_and_keeps_full_write_latch) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.write_cop0(2, ~0ULL);
    CHECK_EQ(cpu.read_cop0(2), 0x3fffffffULL);
    cpu.write_cop0(15, 0x1234567889abcdefULL);
    CHECK_EQ(cpu.read_cop0(15), 0xb22ULL);
    CHECK_EQ(cpu.read_cop0(7), 0x1234567889abcdefULL);
    cpu.gpr[1] = 0x9876543212345678ULL;
    cpu.execute((0x10U << 26) | (4U << 21) | (1U << 16) | (14U << 11));
    CHECK_EQ(cpu.cp0[14], 0x9876543212345678ULL);
    cpu.write_cop0(28, 0xffffffffULL);
    CHECK_EQ(cpu.read_cop0(28), 0xffffffffULL);
    cpu.write_cop0(29, ~0ULL);
    CHECK_EQ(cpu.read_cop0(29), 0ULL);
}

TEST(cpu_count_compare_interrupt_is_sticky_until_compare_write) {
    auto system = machine();
    auto& cpu = system->cpu;
    for (unsigned word = 0; word < 8; ++word)
        instruction(*system, 0x1000 + word * 4, 0);
    u64 fetched = 0;
    CHECK(cpu.read_memory(0xffffffff80001000ULL, 4, fetched, true));
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.write_cop0(9, 100);
    cpu.write_cop0(11, 102);
    for (unsigned step = 0; step < 4; ++step)
        cpu.step();
    CHECK_EQ(cpu.cp0[9], 102ULL);
    CHECK((cpu.cp0[13] & 0x8000U) != 0);
    cpu.step();
    cpu.step();
    CHECK((cpu.cp0[13] & 0x8000U) != 0);
    cpu.write_cop0(11, 500);
    CHECK((cpu.cp0[13] & 0x8000U) == 0);
}

TEST(cpu_enabled_interrupt_vectors_before_instruction_execution) {
    auto system = machine();
    auto& cpu = system->cpu;
    instruction(*system, 0x1000, immediate(9, 0, 1, 7));
    cpu.write_cop0(12, 0x34000101);
    cpu.write_cop0(13, 0x100);
    cpu.step();
    CHECK_EQ(cpu.gpr[1], 0ULL);
    CHECK_EQ(exception_code(cpu), 0U);
    CHECK_EQ(cpu.cp0[14], 0xffffffffa0001000ULL);
    CHECK_EQ(cpu.pc, 0xffffffff80000180ULL);
}

TEST(cpu_reverse_endian_mode_selects_bus_lanes_in_user_mode) {
    auto system = machine();
    auto& cpu = system->cpu;
    map(cpu, 2, 0x00400000, 0x4000, 0x5000);
    system->bus.write(0x4000, 8, 0x0123456789abcdefULL);
    cpu.write_cop0(12, 0x36000010);
    u64 value = 0;
    CHECK(cpu.read_memory(0x00400000, 1, value));
    CHECK_EQ(value, 0xefULL);
    CHECK(cpu.read_memory(0x00400000, 2, value));
    CHECK_EQ(value, 0xcdefULL);
    CHECK(cpu.read_memory(0x00400000, 4, value));
    CHECK_EQ(value, 0x89abcdefULL);
    CHECK(cpu.read_memory(0x00400000, 8, value));
    CHECK_EQ(value, 0x0123456789abcdefULL);
}

TEST(cpu_doubleword_read_from_rcp_freezes_the_bus_without_committing_a_load) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.gpr[1] = 0xffffffffa4000000ULL;
    cpu.gpr[2] = 0x123456789abcdef0ULL;
    instruction(*system, 0x1000, immediate(0x37, 1, 2, 0));
    cpu.step();
    CHECK(cpu.frozen);
    CHECK(!cpu.exception_pending);
    CHECK_EQ(cpu.gpr[2], 0x123456789abcdef0ULL);
    CHECK_EQ(cpu.pc, 0xffffffffa0001000ULL);
    const u64 cycles = cpu.cycles;
    cpu.write_cop0(12, 0x34000101);
    cpu.write_cop0(13, 0x100);
    cpu.step();
    CHECK_EQ(cpu.pc, 0xffffffffa0001000ULL);
    CHECK(cpu.cycles > cycles);
    CHECK(!cpu.exception_pending);
}

TEST(cpu_cache_fill_from_cartridge_freezes_instead_of_reading_rom) {
    auto system = machine();
    u64 value = 0xfeed;
    CHECK(!system->cpu.read_memory(0xffffffff90000000ULL, 4, value));
    CHECK(system->cpu.frozen);
    CHECK(!system->cpu.exception_pending);
    CHECK_EQ(value, 0xfeedULL);
}

TEST(cpu_store_conditional_fault_reports_zero_and_keeps_memory) {
    auto system = machine();
    auto& cpu = system->cpu;
    cpu.linked = true;
    cpu.gpr[1] = 0xffffffffa0004001ULL;
    cpu.gpr[2] = 0x12345678;
    system->bus.write(0x4000, 4, 0xabcdef01);
    cpu.execute(immediate(0x38, 1, 2, 0));
    CHECK_EQ(exception_code(cpu), 5U);
    CHECK_EQ(cpu.gpr[2], 0ULL);
    CHECK_EQ(cpu.cp0[8], 0xffffffffa0004001ULL);
    CHECK_EQ(system->bus.read(0x4000, 4), 0xabcdef01ULL);
}
