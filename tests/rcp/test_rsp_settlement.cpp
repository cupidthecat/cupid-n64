#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>
#include <memory>

namespace {
using namespace cupid;

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(&rsp.memory[0x1000U | (address & 0x0ffcU)], word);
}

void program(System& system, std::initializer_list<u32> words, u32 start = 0) {
    u32 address = start;
    for (const u32 word : words) {
        instruction(system.rsp, address, word);
        address += 4U;
    }
    system.rsp.write_pc(start);
}

void advance_fragmented(System& system, u64 cpu_cycles) {
    for (u64 cycle = 0; cycle < cpu_cycles; ++cycle)
        system.advance(1);
}

void check_equivalent(System& bulk, System& fragmented) {
    bulk.settle();
    fragmented.settle();
    CHECK_EQ(bulk.bus.output_clock(), fragmented.bus.output_clock());
    CHECK_EQ(bulk.bus.rdp.read_register(0x10), fragmented.bus.rdp.read_register(0x10));
    CHECK_EQ(bulk.bus.memory.clock(), fragmented.bus.memory.clock());
    CHECK_EQ(bulk.rsp.pc, fragmented.rsp.pc);
    CHECK_EQ(bulk.rsp.read_register(0x10), fragmented.rsp.read_register(0x10));
    CHECK(bulk.rsp.memory == fragmented.rsp.memory);
}

void prepare_local_loop(System& system, unsigned phase) {
    test::initialize_memory(system);
    system.advance(phase);
    for (unsigned lane = 0; lane < 8; ++lane)
        write_be16(system.rsp.memory.data() + 0x100U + lane * 2U, static_cast<u16>(lane + 1U));
    program(system,
            {
                0x24010100U, // ADDIU at,zero,0x100.
                0xc8212000U, // LQV v1,0(at).
                0x8c220040U, // LW v0,0x40(at).
                0x24420001U, // ADDIU v0,v0,1.
                0xac220040U, // SW v0,0x40(at).
                0x4a010850U, // VADD v1,v1,v1.
                0xe8212001U, // SQV v1,0x10(at).
                0x1000fffaU, // BEQ zero,zero,0x08.
                0U,
            });
    system.rsp.write_register(0x10, 1U); // Clear halt.
}

void prepare_dma_loop(System& system, unsigned phase) {
    test::initialize_memory(system);
    system.advance(phase);
    program(system,
            {
                0x24210001U, // ADDIU at,at,1.
                0xac010080U, // SW at,0x80(zero).
                0x1000fffdU, // BEQ zero,zero,0.
                0U,
            });
    system.rsp.write_register(0x10, 1U); // Clear halt.
    for (u32 byte = 0; byte < 32U; ++byte)
        system.bus.write_ram_byte(0x400U + byte, static_cast<u8>(0xa0U + byte));
    system.rsp.write_register(0x00, 0x300U);
    system.rsp.write_register(0x04, 0x400U);
    system.rsp.write_register(0x08, 31U); // One 32-byte row, four RCP cycles.
}

void check_dma_destination(System& system, bool transferred) {
    for (u32 byte = 0; byte < 32U; ++byte) {
        const u8 expected = transferred ? static_cast<u8>(0xa0U + byte) : 0U;
        CHECK_EQ(system.rsp.memory[0x300U + byte], expected);
    }
}

} // namespace

TEST(rcp_rsp_settlement_bulk_matches_fragmented_local_pipeline_across_phases_and_budgets) {
    constexpr std::array<u64, 8> budgets{3U, 4U, 5U, 8U, 17U, 31U, 64U, 127U};
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (const u64 budget : budgets) {
            auto bulk = std::make_unique<System>();
            auto fragmented = std::make_unique<System>();
            prepare_local_loop(*bulk, phase);
            prepare_local_loop(*fragmented, phase);

            bulk->advance(budget);
            advance_fragmented(*fragmented, budget);
            check_equivalent(*bulk, *fragmented);
            CHECK_EQ(bulk->bus.rdp.read_register(0x10), (static_cast<u64>(phase) + budget) * 2U / 3U);
        }
    }
}

TEST(rcp_rsp_settlement_keeps_shared_register_reads_on_their_issue_cycles) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        auto bulk = std::make_unique<System>();
        auto fragmented = std::make_unique<System>();
        for (auto* system : {bulk.get(), fragmented.get()}) {
            test::initialize_memory(*system);
            system->advance(phase);
            program(*system,
                    {
                        0x40012000U, // MFC0 at,SP_STATUS.
                        0U,
                        0xac010080U, // SW at,0x80(zero).
                        0x40023800U, // MFC0 v0,SP_SEMAPHORE.
                        0U,
                        0xac020084U, // SW v0,0x84(zero).
                        0x40036000U, // MFC0 v1,DPC_CLOCK.
                        0U,
                        0xac030088U, // SW v1,0x88(zero).
                        0x0000000dU, // BREAK.
                    });
            system->rsp.write_register(0x10, 1U); // Clear halt.
        }

        constexpr u64 budget = 96U;
        bulk->advance(budget);
        advance_fragmented(*fragmented, budget);
        check_equivalent(*bulk, *fragmented);

        CHECK_EQ(read_be32(bulk->rsp.memory.data() + 0x80U), 0U);
        CHECK_EQ(read_be32(bulk->rsp.memory.data() + 0x84U), 0U);
        const u32 observed_clock = read_be32(bulk->rsp.memory.data() + 0x88U);
        CHECK(observed_clock != 0U);
        CHECK(observed_clock < bulk->bus.rdp.read_register(0x10));
        CHECK_EQ(bulk->rsp.read_register(0x10) & 3U, 3U);
    }
}

TEST(rcp_rsp_settlement_stops_before_and_on_dma_row_edges) {
    constexpr std::array<u64, 3> before_edge_cpu{5U, 4U, 4U};
    constexpr std::array<u64, 3> at_edge_cpu{6U, 5U, 6U};
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (const bool at_edge : {false, true}) {
            auto bulk = std::make_unique<System>();
            auto fragmented = std::make_unique<System>();
            prepare_dma_loop(*bulk, phase);
            prepare_dma_loop(*fragmented, phase);
            const u64 budget = at_edge ? at_edge_cpu[phase] : before_edge_cpu[phase];

            bulk->advance(budget);
            advance_fragmented(*fragmented, budget);
            check_equivalent(*bulk, *fragmented);
            CHECK_EQ(bulk->rsp.read_register(0x18), at_edge ? 0U : 1U);
            check_dma_destination(*bulk, at_edge);
        }
    }
}

TEST(rcp_rsp_settlement_preserves_single_step_and_halted_branch_bubbles) {
    {
        auto bulk = std::make_unique<System>();
        auto fragmented = std::make_unique<System>();
        for (auto* system : {bulk.get(), fragmented.get()}) {
            test::initialize_memory(*system);
            write_be32(system->rsp.memory.data(), 0xfeedfaceU);
            program(*system,
                    {
                        0x24011234U, // ADDIU at,zero,0x1234.
                        0x4a0000acU, // VXOR v2,v0,v0; would normally pair with scalar work.
                        0xac010000U, // SW at,0(zero).
                        0x0000000dU, // BREAK.
                    });
            system->rsp.write_register(0x10, 0x41U); // Clear halt and set single-step.
        }

        bulk->advance(32U);
        advance_fragmented(*fragmented, 32U);
        check_equivalent(*bulk, *fragmented);
        CHECK_EQ(bulk->rsp.pc, 4U);
        CHECK_EQ(bulk->rsp.read_register(0x10) & 0x23U, 0x21U);
        CHECK_EQ(read_be32(bulk->rsp.memory.data()), 0xfeedfaceU);

        for (auto* system : {bulk.get(), fragmented.get()})
            system->rsp.write_register(0x10, 0x21U); // Clear halt and single-step.
        bulk->advance(12U);
        advance_fragmented(*fragmented, 12U);
        check_equivalent(*bulk, *fragmented);
        CHECK_EQ(read_be32(bulk->rsp.memory.data()), 0x1234U);
        CHECK_EQ(bulk->rsp.read_register(0x10) & 3U, 3U);
    }

    {
        auto bulk = std::make_unique<System>();
        auto fragmented = std::make_unique<System>();
        for (auto* system : {bulk.get(), fragmented.get()}) {
            test::initialize_memory(*system);
            write_be32(system->rsp.memory.data(), 0xfeedfaceU);
            program(*system,
                    {
                        0x10000005U, // BEQ zero,zero,0x18.
                        0x0000000dU, // BREAK in the taken delay slot.
                    });
            instruction(system->rsp, 0x18U, 0xac000000U); // SW zero,0(zero).
            system->rsp.write_register(0x10, 1U);
            system->rsp.tick(1);
            system->rsp.tick(1);
            CHECK_EQ(system->rsp.pc, 0x18U);
            CHECK_EQ(system->rsp.read_register(0x10) & 3U, 3U);
        }

        bulk->advance(32U);
        advance_fragmented(*fragmented, 32U);
        check_equivalent(*bulk, *fragmented);
        CHECK_EQ(read_be32(bulk->rsp.memory.data()), 0xfeedfaceU);

        for (auto* system : {bulk.get(), fragmented.get()})
            system->rsp.write_register(0x10, 5U); // Clear halt and broke.
        bulk->advance(2U);
        advance_fragmented(*fragmented, 2U);
        check_equivalent(*bulk, *fragmented);
        CHECK_EQ(read_be32(bulk->rsp.memory.data()), 0U);
        CHECK_EQ(bulk->rsp.pc, 0x1cU);
    }
}

TEST(rcp_rsp_settlement_honors_imem_and_raw_pc_changes_between_calls) {
    auto bulk = std::make_unique<System>();
    auto fragmented = std::make_unique<System>();
    for (auto* system : {bulk.get(), fragmented.get()}) {
        test::initialize_memory(*system);
        program(*system,
                {
                    0x24210001U, // ADDIU at,at,1.
                    0xac010080U, // SW at,0x80(zero).
                    0x1000fffdU, // BEQ zero,zero,0.
                    0U,
                });
        system->rsp.write_register(0x10, 1U);
    }

    bulk->advance(18U);
    advance_fragmented(*fragmented, 18U);
    check_equivalent(*bulk, *fragmented);

    for (auto* system : {bulk.get(), fragmented.get()}) {
        instruction(system->rsp, 0x40U, 0x24050066U); // ADDIU a1,zero,0x66.
        instruction(system->rsp, 0x44U, 0xac050090U); // SW a1,0x90(zero).
        instruction(system->rsp, 0x48U, 0x0000000dU); // BREAK.
        system->rsp.pc = 0x40U;                       // Raw host SP_PC change between settlement calls.
    }

    bulk->advance(24U);
    advance_fragmented(*fragmented, 24U);
    check_equivalent(*bulk, *fragmented);
    CHECK_EQ(read_be32(bulk->rsp.memory.data() + 0x90U), 0x66U);
    CHECK_EQ(bulk->rsp.read_register(0x10) & 3U, 3U);
}
