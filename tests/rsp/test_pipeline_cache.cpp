#include "cupid/rsp/pipeline.hpp"
#include "test.hpp"

using namespace cupid;

namespace {
constexpr u32 addiu = 0x24010001U;
constexpr u32 vnop = 0x4a000037U;
constexpr u32 lqv = 0xc8012000U;
constexpr u32 vadd = 0x4a010850U;

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
