#include "test.hpp"
#include "test_system.hpp"

#include "cupid/system.hpp"

#include <array>
#include <memory>

using namespace cupid;

namespace {

constexpr u32 rsp_pc_register = 0x04080000U;
constexpr u32 dmem_base = 0x04000000U;

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(&rsp.memory[0x1000U | (address & 0xffcU)], word);
}

u32 pc(System& system) {
    return static_cast<u32>(system.bus.read(rsp_pc_register, 4));
}

u32 status(System& system) {
    return system.rsp.read_register(0x10);
}

u32 dp_clock(System& system) {
    return system.bus.rdp.read_register(0x10);
}

void advance_rsp_cycle(System& system) {
    const u32 before = dp_clock(system);
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        system.advance(1);
        const u32 after = dp_clock(system);
        if (after != before) {
            CHECK_EQ((after - before) & 0x00ffffffU, 1U);
            return;
        }
    }
    CHECK(false);
}

} // namespace

TEST(rsp_pipeline_nested_taken_branch_delay_respects_target_issue_alignment) {
    struct Case {
        u32 inner_jump;
        u32 target;
        u32 pc_after_target_issue;
    };
    constexpr std::array<Case, 2> cases{{
        {0x08000010U, 0x40U, 0x48U}, // J 0x40; target bit 2 clear, so target+4 may pair.
        {0x08000019U, 0x64U, 0x68U}, // J 0x64; target bit 2 set, so target is single-issued.
    }};

    for (const auto& value : cases) {
        auto system = std::make_unique<System>();
        system->bus.write(dmem_base + 0, 4, 0x11111111U);
        system->bus.write(dmem_base + 4, 4, 0x22222222U);
        instruction(system->rsp, 0x00, 0x08000008U);             // J 0x20.
        instruction(system->rsp, 0x04, value.inner_jump);        // Taken branch in the outer delay slot.
        instruction(system->rsp, 0x20, 0xac000000U);             // SW zero,0(zero); inner delay slot.
        instruction(system->rsp, value.target, 0xac000004U);     // SW zero,4(zero).
        instruction(system->rsp, value.target + 4, 0x4a0000acU); // VXOR v2,v0,v0.
        instruction(system->rsp, value.target + 8, 0x0000000dU); // BREAK.
        system->rsp.write_register(0x10, 1);                     // Clear halt.

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x04U);
        CHECK_EQ(dp_clock(*system), 1U);

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x20U);
        CHECK_EQ(dp_clock(*system), 2U);

        advance_rsp_cycle(*system); // Taken-branch delay-slot retirement bubble.
        CHECK_EQ(pc(*system), 0x20U);
        CHECK_EQ(system->bus.read(dmem_base + 0, 4), 0x11111111U);
        CHECK_EQ(dp_clock(*system), 3U);

        advance_rsp_cycle(*system); // Outer target executes as the inner branch's delay slot.
        CHECK_EQ(pc(*system), value.target);
        CHECK_EQ(system->bus.read(dmem_base + 0, 4), 0U);
        CHECK_EQ(system->bus.read(dmem_base + 4, 4), 0x22222222U);
        CHECK_EQ(dp_clock(*system), 4U);

        advance_rsp_cycle(*system); // Inner branch delay-slot retirement bubble.
        CHECK_EQ(pc(*system), value.target);
        CHECK_EQ(dp_clock(*system), 5U);

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), value.pc_after_target_issue);
        CHECK_EQ(system->bus.read(dmem_base + 4, 4), 0U);
        CHECK_EQ(status(*system) & 3U, 0U);
        CHECK_EQ(dp_clock(*system), 6U);

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), value.target + 12U);
        CHECK_EQ(status(*system) & 3U, 3U);
        CHECK_EQ(dp_clock(*system), 7U);
    }
}

TEST(rsp_pipeline_pc_rewrite_discards_latched_words_and_keeps_dependency_age) {
    auto system = std::make_unique<System>();
    system->bus.write(dmem_base + 0, 4, 0xfeedfaceU);
    system->bus.write(dmem_base + 0x80, 4, 0x00001234U);
    instruction(system->rsp, 0x00, 0x8c010080U); // LW r1,0x80(zero).
    instruction(system->rsp, 0x04, 0x00211021U); // ADDU r2,r1,r1.
    instruction(system->rsp, 0x08, 0x4a0000acU); // VXOR v2,v0,v0; pairs with the dependent ADDU.
    instruction(system->rsp, 0x0c, 0x0000000dU); // BREAK.
    system->rsp.write_register(0x10, 1);         // Clear halt.

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x04U);
    CHECK_EQ(dp_clock(*system), 1U);

    advance_rsp_cycle(*system); // The dependent ADDU/VXOR group is latched, then stalls.
    CHECK_EQ(pc(*system), 0x04U);
    CHECK_EQ(system->bus.read(dmem_base + 0, 4), 0xfeedfaceU);
    CHECK_EQ(dp_clock(*system), 2U);

    system->bus.write(rsp_pc_register, 4, 0x04U); // Same-address redirect must discard the latched group.
    CHECK_EQ(pc(*system), 0x04U);
    instruction(system->rsp, 0x04,
                0xac010000U); // SW r1,0(zero); fresh instruction still depends on the load.
    instruction(system->rsp, 0x08, 0x0000000dU); // BREAK.

    advance_rsp_cycle(*system); // Dependency history survived the redirect, so one wait remains.
    CHECK_EQ(pc(*system), 0x04U);
    CHECK_EQ(system->bus.read(dmem_base + 0, 4), 0xfeedfaceU);
    CHECK_EQ(dp_clock(*system), 3U);

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x08U);
    CHECK_EQ(system->bus.read(dmem_base + 0, 4), 0x00001234U);
    CHECK_EQ(dp_clock(*system), 4U);

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x0cU);
    CHECK_EQ(status(*system) & 3U, 3U);
    CHECK_EQ(dp_clock(*system), 5U);
}

TEST(rsp_pipeline_mtc0_halt_retires_paired_vector_op_in_both_orders) {
    struct Pair {
        u32 first;
        u32 second;
    };
    constexpr std::array<Pair, 2> pairs{{
        {0x40812000U, 0x4a0000aaU}, // MTC0 r1,SP_STATUS; VOR v2,v0,v0.
        {0x4a0000aaU, 0x40812000U}, // VOR v2,v0,v0; MTC0 r1,SP_STATUS.
    }};

    for (const auto& pair : pairs) {
        auto system = std::make_unique<System>();
        system->bus.write(dmem_base, 4, 0x12a50000U);
        instruction(system->rsp, 0x00, 0x24010002U); // ADDIU r1,zero,2; SP_STATUS set-halt command.
        instruction(system->rsp, 0x04, 0xc8000000U); // LBV v0[0],0(zero).
        instruction(system->rsp, 0x08, pair.first);
        instruction(system->rsp, 0x0c, pair.second);
        instruction(system->rsp, 0x10, 0xe8020001U); // SBV v2[0],1(zero).
        instruction(system->rsp, 0x14, 0x0000000dU); // BREAK.
        system->rsp.write_register(0x10, 1);         // Clear halt.

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x04U);
        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x08U);

        for (u32 expected_clock : {3U, 4U, 5U}) {
            advance_rsp_cycle(*system); // VOR waits for the vector load to leave the three-stage scoreboard.
            CHECK_EQ(pc(*system), 0x08U);
            CHECK_EQ(system->bus.read(dmem_base + 1, 1), 0xa5U);
            CHECK_EQ(dp_clock(*system), expected_clock);
        }

        advance_rsp_cycle(*system); // Both members retire even when the first one sets HALT.
        CHECK_EQ(pc(*system), 0x10U);
        CHECK_EQ(status(*system) & 3U, 1U);
        CHECK_EQ(system->bus.read(dmem_base + 1, 1), 0xa5U);
        CHECK_EQ(dp_clock(*system), 6U);

        system->rsp.write_register(0x10, 1); // Resume without touching the pipeline scoreboard.
        CHECK_EQ(status(*system) & 3U, 0U);

        for (u32 expected_clock : {7U, 8U, 9U}) {
            advance_rsp_cycle(
                *system); // The paired VOR result remains unavailable for three cycles across HALT.
            CHECK_EQ(pc(*system), 0x10U);
            CHECK_EQ(system->bus.read(dmem_base + 1, 1), 0xa5U);
            CHECK_EQ(dp_clock(*system), expected_clock);
        }

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x14U);
        CHECK_EQ(system->bus.read(dmem_base + 1, 1), 0x12U);
        CHECK_EQ(dp_clock(*system), 10U);

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x18U);
        CHECK_EQ(status(*system) & 3U, 3U);
        CHECK_EQ(dp_clock(*system), 11U);
    }
}

TEST(rsp_pipeline_taken_break_delay_slot_spends_one_bubble_across_halt_resume) {
    for (unsigned halt_gap = 0; halt_gap < 3; ++halt_gap) {
        auto system = std::make_unique<System>();
        system->bus.write(dmem_base, 4, 0xfeedfaceU);
        instruction(system->rsp, 0x00, 0x10000005U); // BEQ zero,zero,0x18.
        instruction(system->rsp, 0x04, 0x0000000dU); // BREAK in the taken delay slot.
        instruction(system->rsp, 0x18, 0xac000000U); // SW zero,0(zero).
        system->rsp.write_register(0x10, 0x101U);    // Clear halt and enable interrupt on BREAK.

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x04U);
        CHECK_EQ(dp_clock(*system), 1U);

        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), 0x18U);
        CHECK_EQ(status(*system) & 0x43U, 0x43U);
        CHECK_EQ(system->bus.read(0x04300008U, 4) & 1U, 1U);
        CHECK_EQ(dp_clock(*system), 2U);

        if (halt_gap == 1U) {
            advance_rsp_cycle(*system);
            CHECK_EQ(pc(*system), 0x18U);
            CHECK_EQ(status(*system) & 0x43U, 0x43U);
            CHECK_EQ(system->bus.read(dmem_base, 4), 0xfeedfaceU);
            CHECK_EQ(dp_clock(*system), 3U);
        } else if (halt_gap == 2U) {
            system->advance(3); // Two RCP cycles in one host advance.
            CHECK_EQ(pc(*system), 0x18U);
            CHECK_EQ(status(*system) & 0x43U, 0x43U);
            CHECK_EQ(system->bus.read(dmem_base, 4), 0xfeedfaceU);
            CHECK_EQ(dp_clock(*system), 4U);
        }

        system->rsp.write_register(0x10, 13U); // Clear halt, broke, and the SP interrupt.
        CHECK_EQ(status(*system) & 3U, 0U);
        CHECK_EQ(system->bus.read(0x04300008U, 4) & 1U, 0U);

        advance_rsp_cycle(*system);
        if (halt_gap != 0U) {
            CHECK_EQ(pc(*system), 0x1cU);
            CHECK_EQ(system->bus.read(dmem_base, 4), 0U);
        } else {
            CHECK_EQ(pc(*system), 0x18U);
            CHECK_EQ(system->bus.read(dmem_base, 4), 0xfeedfaceU);

            advance_rsp_cycle(*system);
            CHECK_EQ(pc(*system), 0x1cU);
            CHECK_EQ(system->bus.read(dmem_base, 4), 0U);
        }
        CHECK_EQ(dp_clock(*system), halt_gap == 2U ? 5U : 4U);
    }
}

TEST(rsp_pipeline_halted_branch_bubble_ages_dependencies_and_dma) {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    instruction(system->rsp, 0x00, 0x24010002U); // ADDIU r1,zero,2; set-halt command.
    instruction(system->rsp, 0x04, 0x14000000U); // BNE zero,zero,0; not taken, forces next single issue.
    instruction(system->rsp, 0x08, 0x4a0000acU); // VXOR v2,v0,v0.
    instruction(system->rsp, 0x0c, 0x10000004U); // BEQ zero,zero,0x20.
    instruction(system->rsp, 0x10, 0x40812000U); // MTC0 r1,SP_STATUS; halt in the taken delay slot.
    instruction(system->rsp, 0x20, 0x4a0010eaU); // VOR v3,v2,v0.
    instruction(system->rsp, 0x24, 0x4a000037U); // VNOP; prevents a same-cycle scalar pair.
    system->rsp.write_register(0x10, 1);         // Clear halt.

    for (const u32 expected_pc : {0x04U, 0x08U, 0x0cU, 0x10U, 0x20U}) {
        advance_rsp_cycle(*system);
        CHECK_EQ(pc(*system), expected_pc);
    }
    CHECK_EQ(status(*system) & 3U, 1U);
    CHECK_EQ(dp_clock(*system), 5U);

    for (u32 byte = 0; byte < 8; ++byte) {
        system->bus.write_ram_byte(0x200U + byte, static_cast<u8>(0xa0U + byte));
        system->rsp.memory[0x80U + byte] = 0;
        CHECK_EQ(system->bus.read_ram_byte(0x200U + byte), static_cast<u8>(0xa0U + byte));
    }
    system->rsp.write_register(0x00, 0x80U);
    system->rsp.write_register(0x04, 0x200U);
    system->rsp.write_register(0x08, 7U);
    CHECK_EQ(system->rsp.read_register(0x18), 1U);

    // This halted RCP cycle is still the taken-branch bubble. It must advance
    // the dependency history and SP DMA without issuing the branch target.
    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x20U);
    CHECK_EQ(status(*system) & 3U, 1U);
    CHECK_EQ(dp_clock(*system), 6U);
    CHECK_EQ(system->rsp.read_register(0x18), 0U);
    CHECK_EQ(system->rsp.read_register(0x00), 0x88U);
    CHECK_EQ(system->rsp.read_register(0x04), 0x208U);
    for (u32 byte = 0; byte < 8; ++byte)
        CHECK_EQ(system->rsp.memory[0x80U + byte], static_cast<u8>(0xa0U + byte));

    system->rsp.write_register(0x10, 1U); // Clear halt.
    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x24U);
    CHECK_EQ(status(*system) & 3U, 0U);
    CHECK_EQ(dp_clock(*system), 7U);
}

TEST(rsp_pipeline_taken_delay_slot_single_step_halt_spends_pending_bubble) {
    auto system = std::make_unique<System>();
    system->bus.write(dmem_base, 4, 0xfeedfaceU);
    instruction(system->rsp, 0x00, 0x10000005U); // BEQ zero,zero,0x18.
    instruction(system->rsp, 0x04, 0x24010055U); // ADDIU r1,zero,0x55; taken delay slot.
    instruction(system->rsp, 0x18, 0xac010000U); // SW r1,0(zero).
    system->rsp.write_register(0x10, 1U);        // Clear halt.

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x04U);
    CHECK_EQ(dp_clock(*system), 1U);

    system->rsp.write_register(0x10, 0x40U); // Enable single-step before the delay slot.
    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x18U);
    CHECK_EQ(status(*system) & 0x23U, 0x21U);
    CHECK_EQ(system->bus.read(dmem_base, 4), 0xfeedfaceU);
    CHECK_EQ(dp_clock(*system), 2U);

    advance_rsp_cycle(*system); // The pending taken-branch bubble elapses while halted.
    CHECK_EQ(pc(*system), 0x18U);
    CHECK_EQ(status(*system) & 0x23U, 0x21U);
    CHECK_EQ(dp_clock(*system), 3U);

    system->rsp.write_register(0x10, 0x21U); // Clear halt and single-step.
    CHECK_EQ(status(*system) & 0x23U, 0U);
    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x1cU);
    CHECK_EQ(system->bus.read(dmem_base, 4), 0x55U);
    CHECK_EQ(dp_clock(*system), 4U);
}

TEST(rsp_pipeline_single_step_suppresses_dual_issue_until_resumed) {
    auto system = std::make_unique<System>();
    system->bus.write(dmem_base, 4, 0xfeedfaceU);
    instruction(system->rsp, 0x00, 0x24011234U); // ADDIU r1,zero,0x1234.
    instruction(system->rsp, 0x04, 0x4a0000acU); // VXOR v2,v0,v0; normally pairs with the ADDIU.
    instruction(system->rsp, 0x08, 0xac010000U); // SW r1,0(zero); pairs with the VXOR after resume.
    instruction(system->rsp, 0x0c, 0x0000000dU); // BREAK.
    system->rsp.write_register(0x10, 0x41U);     // Clear halt and set single-step.

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x04U);
    CHECK_EQ(status(*system) & 0x23U, 0x21U);
    CHECK_EQ(system->bus.read(dmem_base, 4), 0xfeedfaceU);
    CHECK_EQ(dp_clock(*system), 1U);

    system->rsp.write_register(0x10, 0x21U); // Clear halt and single-step.
    CHECK_EQ(status(*system) & 0x23U, 0U);

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x0cU);
    CHECK_EQ(system->bus.read(dmem_base, 4), 0x00001234U);
    CHECK_EQ(dp_clock(*system), 2U);

    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x10U);
    CHECK_EQ(status(*system) & 3U, 3U);
    CHECK_EQ(dp_clock(*system), 3U);

    system->rsp.reset();
    CHECK_EQ(pc(*system), 0U);
    CHECK_EQ(status(*system) & 0x23U, 1U);
    CHECK_EQ(dp_clock(*system),
             3U); // RSP reset clears pipeline/control state without resetting the RDP clock.

    system->bus.write(dmem_base, 1, 0xa5U);
    instruction(system->rsp, 0x00, 0xe8020000U); // SBV v2[0],0(zero).
    system->rsp.write_register(0x10, 1);         // Clear halt.
    advance_rsp_cycle(*system);
    CHECK_EQ(pc(*system), 0x04U);
    CHECK_EQ(system->bus.read(dmem_base, 1), 0U);
    CHECK_EQ(status(*system) & 3U, 0U);
    CHECK_EQ(dp_clock(*system), 4U);
}
