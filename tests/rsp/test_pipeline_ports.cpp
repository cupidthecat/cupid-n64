#include "test.hpp"

#include "cupid/system.hpp"

#include <array>
#include <memory>
#include <span>
#include <vector>

using namespace cupid;

namespace {

constexpr u32 dmem_base = 0x04000000U;
constexpr u32 mfc0_dp_clock_k0 = 0x401a6000U;
constexpr u32 mfc0_dp_clock_k1 = 0x401b6000U;
constexpr u32 break_instruction = 0x0000000dU;
constexpr u32 scalar_nop = 0x00000000U;
constexpr u32 vector_nop = 0x4a000037U;

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(&rsp.memory[0x1000U | (address & 0xffcU)], word);
}

u32 status(System& system) {
    return system.rsp.read_register(0x10);
}

u32 pc_after_one_tick(u32 first, u32 second) {
    auto system = std::make_unique<System>();
    instruction(system->rsp, 0x00, first);
    instruction(system->rsp, 0x04, second);
    instruction(system->rsp, 0x08, break_instruction);
    system->rsp.write_register(0x10, 1);
    system->rsp.tick(1);
    CHECK_EQ(status(*system) & 3U, 0U);
    return system->rsp.pc;
}

struct ClockMeasurement {
    u32 clocks;
    u32 dmem_word;
};

ClockMeasurement measure_scalar_tail(std::span<const u32> body) {
    auto system = std::make_unique<System>();
    system->bus.write(dmem_base, 4, 0xfeedfaceU);
    system->bus.write(dmem_base + 0x80, 4, 0x00001234U);

    std::vector<u32> program{mfc0_dp_clock_k0, scalar_nop, scalar_nop, scalar_nop, scalar_nop};
    program.insert(program.end(), body.begin(), body.end());
    program.insert(program.end(),
                   {scalar_nop, scalar_nop, scalar_nop, scalar_nop, mfc0_dp_clock_k1, scalar_nop, scalar_nop,
                    scalar_nop, scalar_nop, 0xac1a0004U, 0xac1b0008U, break_instruction});
    for (std::size_t index = 0; index < program.size(); ++index)
        instruction(system->rsp, static_cast<u32>(index * 4U), program[index]);

    system->rsp.write_register(0x10, 1);
    system->advance(512);
    CHECK_EQ(status(*system) & 3U, 3U);
    const u32 first = static_cast<u32>(system->bus.read(dmem_base + 4, 4));
    const u32 second = static_cast<u32>(system->bus.read(dmem_base + 8, 4));
    return {(second - first) & 0x00ffffffU, static_cast<u32>(system->bus.read(dmem_base, 4))};
}

u32 measure_vector_tail(std::span<const u32> body) {
    auto system = std::make_unique<System>();
    std::vector<u32> program{mfc0_dp_clock_k0, scalar_nop, scalar_nop, scalar_nop, scalar_nop};
    program.insert(program.end(), body.begin(), body.end());

    // The vector sentinel cannot pair with a vector instruction at the end of
    // the body. It can pair with the first trailing scalar NOP, so the fixed
    // clock framing remains nine issue slots.
    program.insert(program.end(),
                   {vector_nop, scalar_nop, scalar_nop, scalar_nop, scalar_nop, mfc0_dp_clock_k1, scalar_nop,
                    scalar_nop, scalar_nop, scalar_nop, 0xac1a0004U, 0xac1b0008U, break_instruction});
    for (std::size_t index = 0; index < program.size(); ++index)
        instruction(system->rsp, static_cast<u32>(index * 4U), program[index]);

    system->rsp.write_register(0x10, 1);
    system->advance(512);
    CHECK_EQ(status(*system) & 3U, 3U);
    const u32 first = static_cast<u32>(system->bus.read(dmem_base + 4, 4));
    const u32 second = static_cast<u32>(system->bus.read(dmem_base + 8, 4));
    return (second - first) & 0x00ffffffU;
}

} // namespace

TEST(rsp_pipeline_vector_results_wait_for_all_three_scoreboard_stages) {
    constexpr u32 non_taken_bne = 0x14000000U; // BNE zero,zero,0; scalar and forced single issue.
    constexpr u32 producer = 0x4a0000acU;      // VXOR v2,v0,v0.
    constexpr u32 consumer = 0x4a0010eaU;      // VOR v3,v2,v0.

    for (unsigned scalar_gaps = 0; scalar_gaps <= 3; ++scalar_gaps) {
        std::vector<u32> body{non_taken_bne, producer};
        body.insert(body.end(), scalar_gaps, non_taken_bne);
        body.push_back(consumer);

        // The initial branch single-issues the producer. Zero, one, two, and
        // three scalar issue gaps leave three, two, one, and zero vector waits.
        // The body is therefore six slots in every case, plus nine clock slots.
        CHECK_EQ(measure_vector_tail(body), 15U);
    }
}

TEST(rsp_pipeline_cop2_transfers_and_vector_memory_use_the_scalar_issue_unit) {
    constexpr u32 mfc2_r1_v0 = 0x48010000U;
    constexpr u32 lbv_v1 = 0xc8010000U;
    constexpr u32 addiu_r2 = 0x24020001U;
    constexpr u32 vxor_v2 = 0x4a0000acU;

    CHECK_EQ(pc_after_one_tick(mfc2_r1_v0, addiu_r2), 4U);
    CHECK_EQ(pc_after_one_tick(lbv_v1, addiu_r2), 4U);
    CHECK_EQ(pc_after_one_tick(mfc2_r1_v0, vxor_v2), 8U);
    CHECK_EQ(pc_after_one_tick(lbv_v1, vxor_v2), 8U);
}

TEST(rsp_pipeline_vector_and_control_dependencies_block_same_cycle_pairs) {
    constexpr u32 mtc2_r1_v2 = 0x48811000U;
    constexpr u32 mfc2_r1_v2 = 0x48011000U;
    constexpr u32 vxor_v2 = 0x4a0000acU;
    constexpr u32 vxor_v3 = 0x4a0000ecU;
    constexpr u32 vor_v3_v2_v0 = 0x4a0010eaU;
    constexpr u32 vadd_v2 = 0x4a000090U;
    constexpr u32 cfc2_r1_vco = 0x48410000U;
    constexpr u32 ctc2_r1_vco = 0x48c10000U;

    CHECK_EQ(pc_after_one_tick(mtc2_r1_v2, vor_v3_v2_v0), 4U); // Vector RAW.
    CHECK_EQ(pc_after_one_tick(mtc2_r1_v2, vxor_v2), 4U);      // Vector WAW.
    CHECK_EQ(pc_after_one_tick(vxor_v2, mfc2_r1_v2), 4U);      // Reverse-order vector RAW.
    CHECK_EQ(pc_after_one_tick(vadd_v2, cfc2_r1_vco), 4U);     // Control RAW.
    CHECK_EQ(pc_after_one_tick(vadd_v2, ctc2_r1_vco), 4U);     // Control WAW.

    CHECK_EQ(pc_after_one_tick(mtc2_r1_v2, vxor_v3), 8U);
    CHECK_EQ(pc_after_one_tick(ctc2_r1_vco, vxor_v2), 8U);
}

TEST(rsp_pipeline_load_store_port_conflict_waits_at_the_memory_stage) {
    constexpr std::array<u32, 3> scalar_body{
        0x8c010080U, // LW r1,0x80(zero).
        0x24020055U, // ADDIU r2,zero,0x55; bypassed scalar result.
        0xac020000U, // SW r2,0(zero); waits while the load occupies the shared port.
    };
    const auto scalar = measure_scalar_tail(scalar_body);
    CHECK_EQ(scalar.clocks, 13U);
    CHECK_EQ(scalar.dmem_word, 0x00000055U);

    constexpr std::array<u32, 3> vector_memory_body{
        0xc8010000U, // LBV v1[0],0(zero).
        0x24020055U, // Independent scalar gap.
        0xe8000000U, // SBV v0[0],0(zero); same load/store port conflict.
    };
    CHECK_EQ(measure_scalar_tail(vector_memory_body).clocks, 13U);
}

TEST(rsp_pipeline_vnop_and_reciprocal_fake_fields_match_pairing_wires) {
    constexpr u32 mtc2_r1_v2 = 0x48811000U;
    constexpr u32 lbv_v2 = 0xc8020000U;
    constexpr u32 ltv_v0_group = 0xc8005800U;
    constexpr u32 vnop_fake_v2 = 0x4a0000b7U;
    constexpr u32 vnop_fake_v3 = 0x4a0000f7U;
    constexpr u32 vnop_fake_v8 = 0x4a000237U;
    constexpr u32 vrcp_v3_fake_v2 = 0x4a0010f0U;
    constexpr u32 vrcp_v3_fake_v3 = 0x4a0018f0U;

    // VNOP's encoded destination is normally a fake field. MTC2 and LTV are
    // the cases where that wire participates in the pair conflict.
    CHECK_EQ(pc_after_one_tick(lbv_v2, vnop_fake_v2), 8U);
    CHECK_EQ(pc_after_one_tick(mtc2_r1_v2, vnop_fake_v2), 4U);
    CHECK_EQ(pc_after_one_tick(mtc2_r1_v2, vnop_fake_v3), 8U);
    CHECK_EQ(pc_after_one_tick(ltv_v0_group, vnop_fake_v3), 4U);
    CHECK_EQ(pc_after_one_tick(ltv_v0_group, vnop_fake_v8), 8U);

    // Reciprocal/move instructions always expose the encoded VS field to the
    // pairing circuit even though the arithmetic data source is VT.
    CHECK_EQ(pc_after_one_tick(lbv_v2, vrcp_v3_fake_v2), 4U);
    CHECK_EQ(pc_after_one_tick(lbv_v2, vrcp_v3_fake_v3), 8U);
}
