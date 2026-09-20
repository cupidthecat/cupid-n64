#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>

namespace {
using namespace cupid;
constexpr u64 code = 0xffffffffa0001000ULL;
constexpr u64 vector = 0xffffffffbfc00000ULL;

void prepare(System& system) {
    test::initialize_memory(system);
    system.bus.write(0x04700010, 4, 0);
    system.cpu.cp0[12] = 0x10000000;
    system.cpu.set_pc(code);
    system.bus.write(0x1000, 4, 0x34020042); // ORI v0, zero, 0x42
}
} // namespace

TEST(cpu_nmi_ignores_status_masks_and_preserves_normal_exception_registers) {
    for (u32 flags = 0; flags < 32; ++flags) {
        System system;
        prepare(system);
        auto& cpu = system.cpu;
        const u32 status = 0x1220ff00U | flags;
        cpu.cp0[12] = status;
        cpu.cp0[13] = 0xb0008340;
        cpu.cp0[14] = 0x123456789abcdef0ULL;
        cpu.cp0[30] = 0xabcdef0123456789ULL;
        cpu.request_nmi();
        CHECK_EQ(cpu.pc, code);
        cpu.step();
        CHECK_EQ(cpu.pc, vector);
        CHECK_EQ(cpu.cp0[30], code);
        CHECK_EQ(cpu.status(), (status & ~0x00200000U) | 0x00500004U);
        CHECK_EQ(cpu.cp0[13], 0xb0008340U);
        CHECK_EQ(cpu.cp0[14], 0x123456789abcdef0ULL);
        CHECK_EQ(cpu.gpr[2], 0U);
        CHECK_EQ(cpu.instruction_count, 0U);
        CHECK(cpu.exception_pending);
    }
}

TEST(cpu_nmi_saves_the_branch_address_when_a_delay_slot_is_pending) {
    for (u32 branch :
         {0x10000007U, 0x14000007U, 0x50000007U, 0x08000408U, 0x0c000408U, 0x01000008U, 0x0100f809U}) {
        System system;
        prepare(system);
        auto& cpu = system.cpu;
        cpu.gpr[8] = code + 0x20;
        system.bus.write(0x1000, 4, branch);
        system.bus.write(0x1004, 4, 0x34020042);
        cpu.step();
        CHECK_EQ(cpu.pc, code + 4);
        cpu.request_nmi();
        cpu.step();
        CHECK_EQ(cpu.pc, vector);
        CHECK_EQ(cpu.cp0[30], code);
        CHECK_EQ(cpu.cp0[13] & 0x80000000U, 0U);
        write_be32(system.bus.pif.data(), 0x42000018);
        cpu.step();
        CHECK_EQ(cpu.pc, code);
        cpu.step();
        cpu.step();
        CHECK_EQ(cpu.gpr[2], 0x42U);
        CHECK_EQ(cpu.pc, branch == 0x14000007U ? code + 8 : code + 0x20);
    }
}

TEST(cpu_nmi_takes_priority_over_pending_interrupts_and_fetch_faults) {
    for (u64 address : std::array<u64, 3>{0x4000, code + 1, 0xffffffffa4900000ULL}) {
        System system;
        prepare(system);
        auto& cpu = system.cpu;
        cpu.set_pc(address);
        cpu.cp0[12] = 0x10008301;
        cpu.cp0[13] = 0x8300;
        cpu.cp0[8] = 0x1234;
        cpu.request_nmi();
        cpu.step();
        CHECK_EQ(cpu.pc, vector);
        CHECK_EQ(cpu.cp0[30], address);
        CHECK_EQ(cpu.cp0[8], 0x1234U);
        CHECK_EQ(cpu.cp0[13], 0x8300U);
        CHECK(!cpu.frozen);
    }
}

TEST(cpu_cold_reset_clears_the_soft_reset_indicator) {
    System system;
    CHECK_EQ(system.cpu.status() & 0x00700004U, 0x00400004U);
    system.cpu.cp0[12] |= 0x00300000U;
    system.cpu.reset();
    CHECK_EQ(system.cpu.status() & 0x00700004U, 0x00400004U);
}

TEST(cpu_nmi_preserves_registers_tlb_cache_contents_and_load_link_state) {
    System system;
    prepare(system);
    auto& cpu = system.cpu;
    for (unsigned i = 1; i < 32; ++i) {
        cpu.gpr[i] = 0x8765432100000000ULL + i;
        cpu.cp0[i] = 0x12340000U + i;
        cpu.fpu.registers[i] = 0xfedcba9800000000ULL + i;
        cpu.tlb[i] = {0x12345000U + i, {0x345U + i, 0x456U + i}, 0x6000, true};
    }
    cpu.cp0[9] = 100;
    cpu.cp0[11] = 0xffffffff;
    cpu.cp0[12] = 0x1a20001f;
    cpu.cp0[13] = 0x30000340;
    cpu.hi = 0x123456789abcdef0ULL;
    cpu.lo = 0xfedcba9876543210ULL;
    cpu.cop2_latch = 0x13579bdf2468ace0ULL;
    cpu.fpu.control = 0x12340000;
    cpu.linked = true;
    for (unsigned i = 0; i < cpu.data_cache.size(); ++i) {
        cpu.data_cache[i].data.fill(static_cast<u8>(i));
        cpu.data_cache[i].tag = i << 12;
        cpu.data_cache[i].valid = cpu.data_cache[i].dirty = true;
        cpu.instruction_cache[i].data.fill(static_cast<u8>(i ^ 0x5aU));
        cpu.instruction_cache[i].tag = i << 13;
        cpu.instruction_cache[i].valid = true;
    }
    const auto gpr = cpu.gpr;
    auto cp0 = cpu.cp0;
    const auto fpu = cpu.fpu.registers;
    const auto tlb = cpu.tlb;
    const auto data = cpu.data_cache;
    const auto instruction = cpu.instruction_cache;
    cpu.request_nmi();
    cpu.step();
    cp0[12] = 0x1a50001f;
    cp0[30] = code;
    CHECK(cpu.gpr == gpr);
    CHECK(cpu.cp0 == cp0);
    CHECK(cpu.fpu.registers == fpu);
    CHECK_EQ(cpu.fpu.control, 0x12340000U);
    CHECK_EQ(cpu.hi, 0x123456789abcdef0ULL);
    CHECK_EQ(cpu.lo, 0xfedcba9876543210ULL);
    CHECK_EQ(cpu.cop2_latch, 0x13579bdf2468ace0ULL);
    CHECK(cpu.linked);
    for (unsigned i = 0; i < tlb.size(); ++i) {
        CHECK_EQ(cpu.tlb[i].entry_hi, tlb[i].entry_hi);
        CHECK(cpu.tlb[i].entry_lo == tlb[i].entry_lo);
        CHECK_EQ(cpu.tlb[i].page_mask, tlb[i].page_mask);
        CHECK_EQ(cpu.tlb[i].global, tlb[i].global);
    }
    for (unsigned i = 0; i < data.size(); ++i) {
        CHECK(cpu.data_cache[i].data == data[i].data);
        CHECK_EQ(cpu.data_cache[i].tag, data[i].tag);
        CHECK(cpu.data_cache[i].valid && cpu.data_cache[i].dirty);
        CHECK(cpu.instruction_cache[i].data == instruction[i].data);
        CHECK_EQ(cpu.instruction_cache[i].tag, instruction[i].tag);
        CHECK(cpu.instruction_cache[i].valid);
    }
}

TEST(cpu_nmi_requests_coalesce_and_a_new_request_can_interrupt_the_handler) {
    System system;
    prepare(system);
    auto& cpu = system.cpu;
    write_be32(system.bus.pif.data(), 0x34030077); // ORI v1, zero, 0x77
    cpu.request_nmi();
    cpu.request_nmi();
    cpu.step();
    CHECK_EQ(cpu.cp0[30], code);
    cpu.step();
    CHECK_EQ(cpu.gpr[3], 0x77U);
    CHECK_EQ(cpu.pc, vector + 4);
    cpu.request_nmi();
    cpu.step();
    CHECK_EQ(cpu.cp0[30], vector + 4);
    CHECK_EQ(cpu.pc, vector);
    CHECK_EQ(cpu.instruction_count, 1U);
}

TEST(cpu_nmi_eret_returns_through_error_epc_without_clearing_exl_or_changing_epc) {
    for (bool exl : {false, true}) {
        System system;
        prepare(system);
        auto& cpu = system.cpu;
        cpu.cp0[12] |= exl ? 2U : 0U;
        cpu.cp0[14] = code + 0x100;
        cpu.linked = true;
        write_be32(system.bus.pif.data(), 0x42000018); // ERET
        cpu.request_nmi();
        cpu.step();
        CHECK(cpu.linked);
        cpu.step();
        CHECK_EQ(cpu.pc, code);
        CHECK_EQ(cpu.status() & 6U, exl ? 2U : 0U);
        CHECK_EQ(cpu.cp0[14], code + 0x100);
        CHECK(!cpu.linked);
        cpu.step();
        CHECK_EQ(cpu.gpr[2], 0x42U);
    }
}

TEST(cpu_nmi_discards_a_prefetched_instruction_without_invalidating_its_cache_line) {
    System system;
    prepare(system);
    auto& cpu = system.cpu;
    constexpr u64 cached = 0xffffffff80001000ULL;
    cpu.set_pc(cached);
    system.bus.write(0x1000, 4, 0);
    system.bus.write(0x1004, 4, 0x34020011);
    u64 ignored = 0;
    CHECK(cpu.read_memory(cached, 4, ignored, true));
    cpu.step();
    cpu.request_nmi();
    cpu.step();
    auto& line = cpu.instruction_cache[128];
    CHECK(line.valid);
    write_be32(line.data.data() + 4, 0x34020022);
    write_be32(system.bus.pif.data(), 0x42000018);
    cpu.step();
    CHECK_EQ(cpu.pc, cached + 4);
    cpu.step();
    CHECK_EQ(cpu.gpr[2], 0x22U);
    CHECK_EQ(system.bus.read(0x1004, 4), 0x34020011U);
}

TEST(cpu_nmi_after_an_annulled_branch_resumes_after_its_skipped_delay_slot) {
    System system;
    prepare(system);
    system.bus.write(0x1000, 4, 0x54000007); // BNEL zero, zero, 0x1020
    system.bus.write(0x1004, 4, 0x34020011);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 8);
    system.cpu.request_nmi();
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[30], code + 8);
    CHECK_EQ(system.cpu.gpr[2], 0U);
}

TEST(cpu_nmi_entry_advances_clocks_without_executing_an_instruction) {
    System system;
    prepare(system);
    auto& cpu = system.cpu;
    cpu.cp0[9] = 100;
    cpu.cp0[11] = 101;
    for (u64 i = 1; i <= 6; ++i) {
        cpu.request_nmi();
        cpu.step();
        CHECK_EQ(cpu.cycles, i);
        CHECK_EQ(cpu.cp0[9], 100 + i / 2);
        CHECK_EQ(system.bus.read(0x04100010, 4), i * 2 / 3);
        CHECK_EQ(cpu.cp0[13] & 0x8000U, i >= 2 ? 0x8000U : 0U);
        CHECK_EQ(cpu.instruction_count, 0U);
    }
}

TEST(cpu_nmi_preserves_a_frozen_system_bus_and_reset_discards_pending_requests) {
    System system;
    prepare(system);
    auto& cpu = system.cpu;
    cpu.frozen = true;
    cpu.request_nmi();
    cpu.step();
    CHECK_EQ(cpu.pc, vector);
    CHECK(cpu.frozen);
    cpu.step();
    CHECK_EQ(cpu.pc, vector);
    CHECK(!cpu.exception_pending);
    cpu.request_nmi();
    cpu.reset();
    cpu.set_pc(code);
    cpu.step();
    CHECK_EQ(cpu.gpr[2], 0x42U);
    CHECK(!cpu.exception_pending);
}
