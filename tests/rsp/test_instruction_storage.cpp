#include "cupid/rsp/memory.hpp"
#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>

using namespace cupid;

namespace {

constexpr u32 dmem_base = 0x04000000U;
constexpr u32 imem_base = 0x04001000U;
constexpr u32 sp_registers = 0x04040000U;

constexpr u32 vnop = 0x4a000037U;
constexpr u32 break_instruction = 0x0000000dU;

void instruction(System& system, u32 address, u32 word) {
    system.bus.write(imem_base + (address & 0x0ffcU), 4, word);
}

void raw_instruction(u8* memory, u32 address, u32 word) {
    write_be32(memory + 0x1000U + (address & 0x0ffcU), word);
}

void resume(Rsp& rsp, u32 address = 0) {
    rsp.write_pc(address);
    rsp.write_register(0x10, 5U); // Clear HALT and BROKE.
}

void run_to_break(Rsp& rsp, u32 address = 0) {
    resume(rsp, address);
    rsp.tick(16);
    CHECK_EQ(rsp.read_register(0x10) & 3U, 3U);
}

void write_ram_word(System& system, u32 address, u32 word) {
    for (unsigned byte = 0; byte < 4; ++byte)
        system.bus.write_ram_byte(address + byte, static_cast<u8>(word >> ((3U - byte) * 8U)));
}

} // namespace

TEST(rsp_instruction_storage_fill_and_copy_keep_destination_escape_state) {
    RspMemory source;
    const u64 source_revision = source.imem_revision();
    source.fill(0x5aU);
    CHECK(source.imem_trusted());
    CHECK(source.imem_revision() != source_revision);

    RspMemory destination;
    auto* retained = destination.data();
    CHECK(!destination.imem_trusted());
    const u64 escaped_revision = destination.imem_revision();

    destination = source;
    CHECK(!destination.imem_trusted());
    CHECK(destination.imem_revision() != escaped_revision);
    CHECK_EQ(retained[0x1000U], 0x5aU);

    const u64 assigned_revision = destination.imem_revision();
    retained[0x1000U] = 0xa5U;
    const auto& observed = destination;
    CHECK_EQ(observed[0x1000U], 0xa5U);
    CHECK_EQ(destination.imem_revision(), assigned_revision);
}

TEST(rsp_instruction_storage_bus_writes_keep_trust_and_refresh_cached_words) {
    System system;
    CHECK(system.rsp.memory.imem_trusted());

    instruction(system, 0x00U, 0x24010001U); // ADDIU at,zero,1.
    instruction(system, 0x04U, 0xac010000U); // SW at,0(zero).
    instruction(system, 0x08U, break_instruction);
    CHECK(system.rsp.memory.imem_trusted());

    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 1U);

    const u64 before = system.rsp.memory.imem_revision();
    instruction(system, 0x00U, 0x24010009U); // Same dependencies, changed immediate.
    CHECK(system.rsp.memory.imem_trusted());
    CHECK(system.rsp.memory.imem_revision() != before);

    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 9U);
}

TEST(rsp_instruction_storage_cached_trusted_packet_falls_back_after_alias_escape) {
    for (const bool const_alias : {false, true}) {
        System system;
        instruction(system, 0x00U, 0x24010001U);
        instruction(system, 0x04U, 0xac010000U);
        instruction(system, 0x08U, break_instruction);
        run_to_break(system.rsp);
        CHECK(system.rsp.memory.imem_trusted());
        CHECK_EQ(system.bus.read(dmem_base, 4), 1U);

        const auto& observed = system.rsp.memory;
        auto* retained = const_alias ? const_cast<u8*>(observed.data()) : system.rsp.memory.data();
        CHECK(!system.rsp.memory.imem_trusted());
        const u64 escaped_revision = system.rsp.memory.imem_revision();
        raw_instruction(retained, 0x00U, 0x24010007U);
        run_to_break(system.rsp);
        CHECK_EQ(system.bus.read(dmem_base, 4), 7U);
        CHECK_EQ(system.rsp.memory.imem_revision(), escaped_revision);

        raw_instruction(retained, 0x00U, 0x24010009U);
        raw_instruction(retained, 0x04U, 0xac010004U);
        run_to_break(system.rsp);
        CHECK_EQ(system.bus.read(dmem_base, 4), 7U);
        CHECK_EQ(system.bus.read(dmem_base + 4U, 4), 9U);
        CHECK_EQ(system.rsp.memory.imem_revision(), escaped_revision);
    }
}

TEST(rsp_instruction_storage_dma_to_imem_refreshes_cache_without_losing_trust) {
    System system;
    test::initialize_memory(system);
    instruction(system, 0x00U, 0x24010001U);
    instruction(system, 0x04U, 0xac010000U);
    instruction(system, 0x08U, break_instruction);
    instruction(system, 0x0cU, 0U);
    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 1U);

    constexpr u32 source = 0x200U;
    write_ram_word(system, source + 0x00U, 0x2401000bU); // ADDIU at,zero,11.
    write_ram_word(system, source + 0x04U, 0xac010000U);
    write_ram_word(system, source + 0x08U, break_instruction);
    write_ram_word(system, source + 0x0cU, 0U);

    const u64 before = system.rsp.memory.imem_revision();
    system.bus.write(sp_registers + 0x00U, 4, 0x1000U);
    system.bus.write(sp_registers + 0x04U, 4, source);
    system.bus.write(sp_registers + 0x08U, 4, 15U); // One 16-byte row into IMEM.
    system.rsp.tick(2);
    CHECK(system.rsp.memory.imem_trusted());
    CHECK(system.rsp.memory.imem_revision() != before);

    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 11U);
}

TEST(rsp_instruction_storage_retained_raw_alias_stays_exact_across_reset) {
    System system;
    auto* retained = system.rsp.memory.data();
    CHECK(!system.rsp.memory.imem_trusted());

    raw_instruction(retained, 0x00U, 0x24010003U);
    raw_instruction(retained, 0x04U, 0xac010000U);
    raw_instruction(retained, 0x08U, break_instruction);
    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 3U);

    system.rsp.reset();
    CHECK(!system.rsp.memory.imem_trusted());
    const u64 reset_revision = system.rsp.memory.imem_revision();

    raw_instruction(retained, 0x00U, 0x24010007U);
    raw_instruction(retained, 0x04U, 0xac010000U);
    raw_instruction(retained, 0x08U, break_instruction);
    CHECK_EQ(system.rsp.memory.imem_revision(), reset_revision);

    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 7U);
}

TEST(rsp_instruction_storage_retained_const_alias_stays_exact_after_const_cast_write) {
    System system;
    const RspMemory& view = system.rsp.memory;
    const u8* retained = view.data();
    CHECK(!system.rsp.memory.imem_trusted());

    auto* writable = const_cast<u8*>(retained);
    raw_instruction(writable, 0x00U, 0x24010004U);
    raw_instruction(writable, 0x04U, 0xac010000U);
    raw_instruction(writable, 0x08U, break_instruction);
    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 4U);

    system.rsp.reset();
    CHECK(!system.rsp.memory.imem_trusted());
    const u64 reset_revision = system.rsp.memory.imem_revision();

    raw_instruction(writable, 0x00U, 0x2401000dU);
    raw_instruction(writable, 0x04U, 0xac010000U);
    raw_instruction(writable, 0x08U, break_instruction);
    CHECK_EQ(system.rsp.memory.imem_revision(), reset_revision);

    run_to_break(system.rsp);
    CHECK_EQ(system.bus.read(dmem_base, 4), 13U);
}

TEST(rsp_instruction_storage_const_reference_and_iterator_aliases_poison_trust) {
    RspMemory reference_memory;
    const RspMemory& reference_view = reference_memory;
    const u8& reference = reference_view[0];
    CHECK_EQ(reference, 0U);
    CHECK(!reference_memory.imem_trusted());

    RspMemory iterator_memory;
    const RspMemory& iterator_view = iterator_memory;
    const auto iterator = iterator_view.cbegin();
    CHECK_EQ(*iterator, 0U);
    CHECK(!iterator_memory.imem_trusted());
}

TEST(rsp_instruction_storage_word_zero_write_refreshes_packet_wrapped_from_ffc) {
    System system;
    instruction(system, 0x0ffcU, vnop);
    instruction(system, 0x0000U, 0x24010001U); // Pairs across the IMEM wrap.
    instruction(system, 0x0004U, 0xac010000U);
    instruction(system, 0x0008U, break_instruction);

    run_to_break(system.rsp, 0x0ffcU);
    CHECK_EQ(system.bus.read(dmem_base, 4), 1U);

    const u64 before = system.rsp.memory.imem_revision();
    instruction(system, 0x0000U, 0x24010002U);
    CHECK(system.rsp.memory.imem_revision() != before);
    run_to_break(system.rsp, 0x0ffcU);
    CHECK_EQ(system.bus.read(dmem_base, 4), 2U);
}

TEST(rsp_instruction_storage_keeps_both_pairing_variants_for_trusted_imem) {
    System system;
    instruction(system, 0x00U, vnop);
    instruction(system, 0x04U, 0x24010001U); // Independent scalar op pairs with VNOP.
    instruction(system, 0x08U, break_instruction);

    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 0x41U); // Clear HALT, set single-step.
    system.rsp.tick(1);
    CHECK_EQ(system.rsp.pc, 4U);
    CHECK_EQ(system.rsp.read_register(0x10) & 1U, 1U);

    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 0x21U); // Clear HALT and single-step.
    system.rsp.tick(1);
    CHECK_EQ(system.rsp.pc, 8U);
    CHECK(system.rsp.memory.imem_trusted());
}

TEST(rsp_instruction_storage_trusted_write_does_not_replace_latched_wait_packet) {
    System system;
    system.bus.write(dmem_base + 0x80U, 4, 0x00001234U);
    instruction(system, 0x00U, 0x8c010080U); // LW at,0x80(zero).
    instruction(system, 0x04U, 0x00211021U); // ADDU v0,at,at.
    instruction(system, 0x08U, vnop);        // Pair with the dependent ADDU.
    instruction(system, 0x0cU, 0xac020000U); // SW v0,0(zero).
    instruction(system, 0x10U, break_instruction);

    resume(system.rsp);
    system.rsp.tick(1); // Retire the load.
    CHECK_EQ(system.rsp.pc, 4U);
    system.rsp.tick(1); // Latch the dependent pair, then spend its first wait.
    CHECK_EQ(system.rsp.pc, 4U);

    const u64 before = system.rsp.memory.imem_revision();
    instruction(system, 0x04U, 0x24020007U); // Fresh fetch should set v0=7.
    CHECK(system.rsp.memory.imem_trusted());
    CHECK(system.rsp.memory.imem_revision() != before);

    system.rsp.tick(12);
    CHECK_EQ(system.bus.read(dmem_base, 4), 0x2468U); // The already-latched ADDU retired.

    run_to_break(system.rsp, 0x04U);
    CHECK_EQ(system.bus.read(dmem_base, 4), 7U); // A later fetch observes the write.
}
