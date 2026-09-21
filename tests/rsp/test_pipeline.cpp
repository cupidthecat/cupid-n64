#include "test.hpp"

#include "cupid/system.hpp"

#include <array>
#include <memory>
#include <span>
#include <vector>

using namespace cupid;

namespace {

struct Measurement {
    u32 clocks;
    u32 value;
};

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(&rsp.memory[0x1000U | (address & 0xffcU)], word);
}

Measurement measure(std::span<const u32> body, bool split = false) {
    auto system = std::make_unique<System>();
    write_be32(system->rsp.memory.data() + 0x80, 0x1234U);
    std::vector<u32> program{0x401a6000U, 0, 0, 0, 0}; // MFC0 k0, DP_CLOCK; four NOPs.
    program.insert(program.end(), body.begin(), body.end());
    program.insert(program.end(), {0, 0, 0, 0, 0x401b6000U, // MFC0 k1, DP_CLOCK.
                                   0, 0, 0, 0, 0xac020000U, 0xac1a0004U, 0xac1b0008U, 0x0000000dU});
    for (std::size_t index = 0; index < program.size(); ++index)
        instruction(system->rsp, static_cast<u32>(index * 4U), program[index]);
    system->rsp.write_register(0x10, 1);
    if (split) {
        for (unsigned cycle = 0; cycle < 256; ++cycle)
            system->advance(1);
    } else {
        system->advance(256);
    }
    CHECK_EQ(system->rsp.read_register(0x10) & 3U, 3U);
    const auto* memory = system->rsp.memory.data();
    return {(read_be32(memory + 8) - read_be32(memory + 4)) & 0xffffffU, read_be32(memory)};
}

} // namespace

TEST(rsp_pipeline_scalar_load_use_stalls_until_the_result_is_ready) {
    constexpr std::array<u32, 2> body{0x8c010080U, 0x00211021U}; // LW r1; ADDU r2,r1,r1.
    for (bool split : {false, true}) {
        const auto result = measure(body, split);
        CHECK_EQ(result.value, 0x2468U);
        CHECK_EQ(result.clocks, 13U); // Nine framing instructions, two operations, two stalls.
    }
}

TEST(rsp_pipeline_one_independent_instruction_hides_one_load_stall) {
    constexpr std::array<u32, 3> body{0x8c010080U, 0, 0x00211021U};
    const auto result = measure(body);
    CHECK_EQ(result.value, 0x2468U);
    CHECK_EQ(result.clocks, 13U);
}

TEST(rsp_pipeline_two_independent_instructions_hide_the_load_latency) {
    constexpr std::array<u32, 4> body{0x8c010080U, 0, 0, 0x00211021U};
    const auto result = measure(body);
    CHECK_EQ(result.value, 0x2468U);
    CHECK_EQ(result.clocks, 13U);
}

TEST(rsp_pipeline_scalar_alu_results_bypass_load_interlocks) {
    constexpr std::array<u32, 2> body{0x24011234U, 0x00211021U};
    const auto result = measure(body);
    CHECK_EQ(result.value, 0x2468U);
    CHECK_EQ(result.clocks, 11U);
}

TEST(rsp_pipeline_unrelated_register_and_zero_register_do_not_stall) {
    for (const u32 load : {0x8c010080U, 0x8c000080U}) {
        const std::array<u32, 2> body{load, 0x24020001U};
        const auto result = measure(body);
        CHECK_EQ(result.value, 1U);
        CHECK_EQ(result.clocks, 11U);
    }
}

TEST(rsp_pipeline_cp0_transfer_result_has_scalar_load_latency) {
    constexpr std::array<u32, 2> body{0x40012000U, 0x00201025U}; // MFC0 r1,SP_STATUS; OR r2,r1,r0.
    const auto result = measure(body);
    CHECK_EQ(result.value, 0U);
    CHECK_EQ(result.clocks, 13U);
}

TEST(rsp_pipeline_independent_scalar_and_vector_instructions_share_a_cycle) {
    auto system = std::make_unique<System>();
    instruction(system->rsp, 0, 0x24011234U);  // ADDIU r1,r0,0x1234.
    instruction(system->rsp, 4, 0x4a0000acU);  // VXOR v2,v0,v0.
    instruction(system->rsp, 8, 0xac010000U);  // SW r1,0(r0).
    instruction(system->rsp, 12, 0x0000000dU); // BREAK.
    system->rsp.write_register(0x10, 1);
    system->rsp.tick(1);
    CHECK_EQ(system->rsp.pc, 8U);
    system->rsp.tick(8);
    CHECK_EQ(read_be32(system->rsp.memory.data()), 0x1234U);
    CHECK_EQ(system->rsp.read_register(0x10) & 3U, 3U);
}

TEST(rsp_pipeline_mixed_instruction_loop_preserves_cpu_and_rsp_clock_ratio) {
    constexpr u32 iterations = 0x1000U;
    constexpr std::array<u32, 22> program{
        0x24011000U, 0,           0,           0,           0,
        0,                           // ADDIU r1,zero,4096; pipeline isolation.
        0x401a6000U, 0,              // MFC0 k0,DPC_CLOCK; NOP.
        0x2421ffffU, 0x4a0000acU,    // ADDIU r1,r1,-1; VXOR v2,v0,v0.
        0x24420001U, 0x4a0000ecU,    // ADDIU r2,r2,1; VXOR v3,v0,v0.
        0x1420fffbU, 0,              // BNE r1,zero,0x20; NOP delay slot.
        0x401b6000U, 0,           0, // MFC0 k1,DPC_CLOCK; isolate scalar result.
        0xac1a0000U, 0xac1b0004U, 0xac020008U, 0xac01000cU, 0x0000000dU,
    };
    // Each taken iteration has two pairs, a branch, a delay slot and a bubble.
    // The final iteration omits the bubble; setup and result stores add 16 cycles.
    constexpr u64 rcp_cycles = 5U * iterations + 15U;
    constexpr u64 cpu_cycles = (3U * rcp_cycles + 1U) / 2U;
    for (bool split : {false, true}) {
        auto system = std::make_unique<System>();
        for (std::size_t index = 0; index < program.size(); ++index)
            instruction(system->rsp, static_cast<u32>(index * 4U), program[index]);
        system->rsp.write_register(0x10, 1);
        if (split) {
            for (u64 cycle = 0; cycle < cpu_cycles; ++cycle)
                system->advance(1);
        } else {
            system->advance(cpu_cycles);
        }
        CHECK_EQ(system->rsp.read_register(0x10) & 3U, 3U);
        CHECK_EQ(system->rsp.pc, 0x58U);
        CHECK_EQ(system->bus.rdp.read_register(0x10), rcp_cycles);
        const auto* memory = system->rsp.memory.data();
        CHECK_EQ(read_be32(memory + 4) - read_be32(memory), 5U * iterations + 1U);
        CHECK_EQ(read_be32(memory + 8), iterations);
        CHECK_EQ(read_be32(memory + 12), 0U);
    }
}
