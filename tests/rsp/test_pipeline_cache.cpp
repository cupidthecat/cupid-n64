#include "cupid/rsp/pipeline.hpp"
#include "test.hpp"

using namespace cupid;

namespace {
constexpr u32 addiu = 0x24010001U;
constexpr u32 vnop = 0x4a000037U;
constexpr u32 lqv = 0xc8012000U;
constexpr u32 vadd = 0x4a010850U;
constexpr u32 mfc0 = 0x40025800U;
constexpr u32 break_instruction = 0x0000000dU;

void drain(RspPipeline& pipeline) {
    pipeline.redirect();
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        pipeline.fetch(0, 0, false);
        while (pipeline.advance_operand_wait()) {
        }
        pipeline.retire(false, 0);
    }
}
} // namespace

TEST(rsp_decoded_fetch_observes_changes_to_both_instruction_words) {
    RspPipeline pipeline;
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
        drain(pipeline);
        pipeline.fetch(addiu, vadd, false);
        CHECK_EQ(pipeline.size(), 2U);
        CHECK_EQ(pipeline.instruction(1), vadd);
        pipeline.retire(false, 8);

        pipeline.fetch(lqv, vadd, false);
        CHECK_EQ(pipeline.size(), 1U); // The vector load conflicts with VADD's input.
        CHECK_EQ(pipeline.instruction(0), lqv);
        pipeline.retire(false, 4);

        drain(pipeline);
        pipeline.fetch(addiu, addiu, false);
        CHECK_EQ(pipeline.size(), 1U);
        pipeline.retire(false, 4);
    }
}

TEST(rsp_decoded_fetch_invalidates_when_only_the_second_word_changes) {
    RspPipeline pipeline;
    constexpr u32 address = 0x180U;

    drain(pipeline);
    pipeline.fetch(addiu, vadd, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.instruction(1), vadd);
    pipeline.retire(false, address + 8U);

    pipeline.fetch(addiu, addiu, false, address);
    CHECK_EQ(pipeline.size(), 1U);
    CHECK_EQ(pipeline.instruction(0), addiu);
    pipeline.retire(false, address + 4U);

    pipeline.fetch(addiu, vadd, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.instruction(1), vadd);
}

TEST(rsp_decoded_fetch_refreshes_semantics_when_ports_stay_the_same) {
    constexpr u32 address = 0x1c0U;
    constexpr u32 addiu_one = 0x24010001U;
    constexpr u32 ori_seven = 0x34010007U;
    RspPipeline pipeline;

    pipeline.fetch(addiu_one, vnop, false, address);
    CHECK_EQ(pipeline.operation(0), RspPipeline::Operation::Addiu);
    CHECK_EQ(static_cast<u16>(pipeline.instruction(0)), 1U);
    pipeline.retire(false, address + 8U);

    // Both instructions have the same scalar dependency shape. The raw word still
    // has to invalidate the cached execution metadata.
    pipeline.fetch(ori_seven, vnop, false, address);
    CHECK_EQ(pipeline.operation(0), RspPipeline::Operation::Ori);
    CHECK_EQ(static_cast<u16>(pipeline.instruction(0)), 7U);
}

TEST(rsp_decoded_fetch_keeps_single_step_and_branch_pairing_restrictions) {
    RspPipeline pipeline;
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
        drain(pipeline);
        pipeline.fetch(addiu, vnop, false);
        CHECK_EQ(pipeline.size(), 2U);
        pipeline.retire(false, 8);
        pipeline.fetch(addiu, vnop, true);
        CHECK_EQ(pipeline.size(), 1U);
        pipeline.retire(false, 4);
        pipeline.fetch(addiu, vnop, false);
        CHECK_EQ(pipeline.size(), 2U);
        pipeline.retire(false, 8);

        pipeline.fetch(0x10000002U, vnop, false);
        CHECK_EQ(pipeline.size(), 1U);
        pipeline.retire(false, 4);
        pipeline.fetch(addiu, vnop, false);
        CHECK_EQ(pipeline.size(), 1U); // The branch delay slot issues alone.
        pipeline.retire(true, 12);
        CHECK(pipeline.advance_branch_wait());
        pipeline.fetch(addiu, vnop, false);
        CHECK_EQ(pipeline.size(), 1U); // An unaligned branch target also issues alone.
        pipeline.retire(false, 16);
    }
}

TEST(rsp_decoded_fetch_retains_load_interlocks_after_reuse_and_collisions) {
    RspPipeline pipeline;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        for (u32 immediate = 0; immediate < 512; ++immediate) {
            pipeline.fetch(0x24010000U | immediate, vnop, false, immediate * 4);
            while (pipeline.advance_operand_wait()) {
            }
            pipeline.retire(false, 8);
        }
        drain(pipeline);
        pipeline.fetch(0x8c010000U, vnop, false); // LW at,0(zero).
        CHECK_EQ(pipeline.size(), 2U);
        pipeline.retire(false, 8);
        pipeline.fetch(0x00201021U, vnop, false); // ADDU v0,at,zero.
        CHECK_EQ(pipeline.size(), 2U);
        CHECK(pipeline.advance_operand_wait());
        CHECK(pipeline.advance_operand_wait());
        CHECK(!pipeline.advance_operand_wait());
        pipeline.retire(false, 16);
    }
}

TEST(rsp_decoded_fetch_leaves_latched_packets_unchanged_until_retirement) {
    RspPipeline pipeline;
    constexpr u32 address = 0x180U;
    pipeline.fetch(lqv, vnop, false);
    pipeline.retire(false, 8);
    pipeline.fetch(vadd, addiu, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK(pipeline.advance_operand_wait());
    pipeline.fetch(0x0000000dU, 0x40015800U, true, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.instruction(0), vadd);
    CHECK_EQ(pipeline.instruction(1), addiu);
    while (pipeline.advance_operand_wait()) {
    }
    pipeline.retire(false, 16);

    // Once the packet retires, the same cache slot observes the changed IMEM words.
    pipeline.fetch(0x0000000dU, 0x40015800U, true, address);
    CHECK_EQ(pipeline.size(), 1U);
    CHECK_EQ(pipeline.instruction(0), 0x0000000dU);
    pipeline.retire(false, address + 4U);

    pipeline.reset();
    pipeline.fetch(0x0000000dU, vnop, false);
    CHECK_EQ(pipeline.size(), 1U);
    CHECK_EQ(pipeline.instruction(0), 0x0000000dU);
    CHECK(!pipeline.advance_operand_wait());
}

TEST(rsp_decoded_fetch_checks_words_when_instruction_addresses_wrap) {
    RspPipeline pipeline;
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
        drain(pipeline);
        pipeline.fetch(addiu, vnop, false, 0x0ffc);
        CHECK_EQ(pipeline.size(), 2U);
        pipeline.retire(false, 4);
        pipeline.fetch(0x10000002U, vnop, false, 0x1ffc);
        CHECK_EQ(pipeline.size(), 1U);
        CHECK_EQ(pipeline.instruction(0), 0x10000002U);
        pipeline.retire(false, 0);
    }
}

TEST(rsp_decoded_fetch_latched_cache_index_is_independent_after_pipeline_copy) {
    constexpr u32 address = 0x240U;
    RspPipeline original;
    original.fetch(lqv, vnop, false, address - 8U);
    original.retire(false, address);
    original.fetch(vadd, addiu, false, address);
    CHECK_EQ(original.size(), 2U);
    CHECK(original.advance_operand_wait());

    RspPipeline copied = original;
    original.redirect();
    original.fetch(0x0000000dU, 0x40015800U, true, address);
    CHECK_EQ(original.size(), 1U);
    CHECK_EQ(original.instruction(0), 0x0000000dU);

    CHECK_EQ(copied.size(), 2U);
    CHECK_EQ(copied.instruction(0), vadd);
    CHECK_EQ(copied.instruction(1), addiu);
    CHECK(copied.advance_operand_wait());
    CHECK(copied.advance_operand_wait());
    CHECK(!copied.advance_operand_wait());
    copied.retire(false, address + 8U);
}

TEST(rsp_decoded_fetch_redirect_and_reset_keep_latch_state_self_contained) {
    constexpr u32 address = 0x300U;
    RspPipeline pipeline;

    pipeline.fetch(addiu, vadd, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    pipeline.redirect();
    CHECK_EQ(pipeline.size(), 0U);
    pipeline.fetch(addiu, vadd, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.instruction(0), addiu);
    CHECK_EQ(pipeline.instruction(1), vadd);

    pipeline.reset();
    CHECK_EQ(pipeline.size(), 0U);
    pipeline.fetch(addiu, vadd, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.instruction(0), addiu);
    CHECK_EQ(pipeline.instruction(1), vadd);
    CHECK(!pipeline.advance_operand_wait());
}

TEST(rsp_decoded_fetch_fresh_local_safety_tracks_both_raw_words) {
    constexpr u32 address = 0x180U;
    RspPipeline pipeline;
    using LocalIssue = RspPipeline::LocalIssue;

    CHECK_EQ(pipeline.local_issue(addiu, mfc0, address), LocalIssue::Blocked);
    CHECK_EQ(pipeline.local_issue(addiu, break_instruction, address), LocalIssue::Blocked);
    CHECK_EQ(pipeline.local_issue(mfc0, vnop, address), LocalIssue::Blocked);
    CHECK_EQ(pipeline.local_issue(break_instruction, vnop, address), LocalIssue::Blocked);

    CHECK_EQ(pipeline.local_issue(addiu, vnop, address), LocalIssue::Ready);
    CHECK_EQ(pipeline.size(), 2U);
}

TEST(rsp_decoded_fetch_latched_local_safety_uses_only_issued_words) {
    constexpr u32 address = 0x200U;
    RspPipeline pipeline;
    using LocalIssue = RspPipeline::LocalIssue;

    // Both words are scalar, so only ADDIU issues. A normal RSP fetch may latch
    // that instruction even though a fresh local co-run would reject the raw COP0
    // in slot two. Once latched, only the issued instruction controls locality.
    CHECK_EQ(pipeline.local_issue(addiu, mfc0, address), LocalIssue::Blocked);
    pipeline.fetch(addiu, mfc0, false, address);
    CHECK_EQ(pipeline.size(), 1U);
    CHECK_EQ(pipeline.local_issue(), LocalIssue::Ready);
    pipeline.retire(false, address + 4U);

    // A vector/scalar pair can issue together, so the same shared instruction is
    // part of the live packet and must stop local execution.
    pipeline.fetch(vnop, mfc0, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.local_issue(), LocalIssue::Blocked);
    pipeline.retire(false, address + 8U);

    pipeline.fetch(vnop, break_instruction, false, address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.local_issue(), LocalIssue::Blocked);
}

TEST(rsp_decoded_fetch_local_safety_revalidates_colliding_cache_entries) {
    RspPipeline pipeline;
    using LocalIssue = RspPipeline::LocalIssue;
    constexpr u32 first_address = 0x0fc0U;
    constexpr u32 colliding_address = first_address + 0x1000U;

    CHECK_EQ(pipeline.local_issue(addiu, mfc0, colliding_address), LocalIssue::Blocked);
    CHECK_EQ(pipeline.local_issue(addiu, vnop, first_address), LocalIssue::Ready);
    pipeline.retire(false, first_address + 8U);

    pipeline.fetch(vnop, addiu, false, first_address);
    CHECK_EQ(pipeline.size(), 2U);
    CHECK_EQ(pipeline.local_issue(), LocalIssue::Ready);

    // A probe cannot replace metadata belonging to an already-latched packet.
    CHECK_EQ(pipeline.local_issue(mfc0, break_instruction, colliding_address), LocalIssue::Blocked);
    CHECK_EQ(pipeline.instruction(0), vnop);
    CHECK_EQ(pipeline.instruction(1), addiu);
    CHECK_EQ(pipeline.local_issue(), LocalIssue::Ready);
}

TEST(rsp_decoded_fetch_local_issue_checks_shared_words_before_branch_bubble) {
    using LocalIssue = RspPipeline::LocalIssue;
    RspPipeline pipeline;

    // Retire a taken branch and its delay slot to create the one-cycle target bubble.
    pipeline.fetch(0x10000007U, vnop, false, 0x000U); // BEQ zero,zero,0x20.
    CHECK_EQ(pipeline.size(), 1U);
    pipeline.retire(false, 0x004U);
    pipeline.fetch(addiu, vnop, false, 0x004U);
    CHECK_EQ(pipeline.size(), 1U);
    pipeline.retire(true, 0x020U);

    // A shared raw slot stops local co-run before the pending bubble is consumed.
    CHECK_EQ(pipeline.local_issue(addiu, mfc0, 0x020U), LocalIssue::Blocked);
    CHECK(pipeline.advance_branch_wait());

    // Recreate the bubble. With two local raw words, local_issue consumes the bubble
    // without latching a packet, then latches that same prepared decode next cycle.
    pipeline.fetch(0x10000007U, vnop, false, 0x000U);
    pipeline.retire(false, 0x004U);
    pipeline.fetch(addiu, vnop, false, 0x004U);
    pipeline.retire(true, 0x020U);
    CHECK_EQ(pipeline.local_issue(addiu, vnop, 0x020U), LocalIssue::Advanced);
    CHECK_EQ(pipeline.size(), 0U);
    CHECK_EQ(pipeline.local_issue(addiu, vnop, 0x020U), LocalIssue::Ready);
    CHECK_EQ(pipeline.size(), 2U);
}
