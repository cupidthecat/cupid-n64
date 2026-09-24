#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <memory>

using namespace cupid;

namespace {

constexpr u32 special(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned function) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | (sa << 6U) | function;
}

constexpr u32 immediate(unsigned opcode, unsigned rs, unsigned rt, int value) {
    return (opcode << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

void prepare(System& system, u32 start = 0) {
    constexpr std::array<u32, 16> program{
        0x8c010100U, // LW at,0x100(zero).
        0x00211021U, // ADDU v0,at,at.
        0xac020100U, // SW v0,0x100(zero).
        0xc8012010U, // LQV v1,0x100(zero).
        0x4a010850U, // VADD v1,v1,v1.
        0xe8012011U, // SQV v1,0x110(zero).
        0x24840001U, // ADDIU a0,a0,1.
        0x34850055U, // ORI a1,a0,0x55.
        0xa405000fU, // SH a1,0xf(zero).
        0x24060fffU, // ADDIU a2,zero,0xfff.
        0xe8c10800U, // SSV v1[0],0(a2).
        0x8c070110U, // LW a3,0x110(zero).
        0x00e44026U, // XOR t0,a3,a0.
        0xac080120U, // SW t0,0x120(zero).
        0x1000fff1U, // BEQ zero,zero,0.
        0U,
    };
    for (unsigned index = 0; index < program.size(); ++index)
        system.bus.write(0x04001000U + ((start + index * 4U) & 0x0fffU), 4, program[index]);
    for (u32 offset = 0; offset < 16; offset += 4)
        system.bus.write(0x04000100U + offset, 4, 0x12345678U + offset);
    system.rsp.write_pc(start);
    system.rsp.write_register(0x10, 1U);
}

template <std::size_t Size>
void prepare_scalar_program(System& system, const std::array<u32, Size>& program) {
    for (unsigned index = 0; index < program.size(); ++index)
        system.bus.write(0x04001000U + index * 4U, 4, program[index]);
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 1U);
}

void compare(System& batched, System& stepped, u64 cycles) {
    batched.advance(cycles);
    for (u64 cycle = 0; cycle < cycles; ++cycle)
        stepped.advance(1);
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10), stepped.rsp.read_register(0x10));
    for (u32 address = 0; address < 4096; address += 4)
        CHECK_EQ(batched.bus.read(0x04000000U + address, 4), stepped.bus.read(0x04000000U + address, 4));
    CHECK(batched.rsp.memory.imem_trusted());
    CHECK(stepped.rsp.memory.imem_trusted());
}

} // namespace

TEST(rsp_local_blocks_preserve_partial_stalls_pairing_and_wrapped_stores) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [batched, stepped] = *machines;
        for (auto* system : {&batched, &stepped}) {
            system->advance(phase);
            prepare(*system);
        }
        for (const u64 cycles : {384U, 1U, 2U, 3U, 16U, 63U, 96U, 127U, 1024U, 17U})
            compare(batched, stepped, cycles);
    }
}

TEST(rsp_local_blocks_revalidate_an_interior_imem_write) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped})
        prepare(*system);
    compare(batched, stepped, 384);
    for (auto* system : {&batched, &stepped}) {
        system->rsp.write_pc(0);
        system->bus.write(0x0400101cU, 4, 0x348500aaU); // ORI a1,a0,0xaa; entry word is unchanged.
    }
    compare(batched, stepped, 384);
    CHECK_EQ(batched.bus.read(0x0400000fU, 2) & 0xaaU, 0xaaU);
}

TEST(rsp_local_blocks_cross_imem_wrap_at_partial_boundaries) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped})
        prepare(*system, 0xfe0U);
    for (const u64 cycles : {384U, 1U, 127U, 1024U})
        compare(batched, stepped, cycles);
}

TEST(rsp_local_blocks_include_incoming_operand_history) {
    constexpr std::array<u64, 3> budgets{96U, 95U, 93U};
    for (unsigned age = 0; age < budgets.size(); ++age) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [batched, stepped] = *machines;
        for (auto* system : {&batched, &stepped}) {
            for (u32 offset = 0; offset < 64; offset += 4)
                system->bus.write(0x04001000U + offset, 4, 0x4a010850U); // VADD v1,v1,v1.
            system->bus.write(0x04001040U, 4, 0x4a000037U); // VNOP separates the final VADD from BREAK.
            system->bus.write(0x04001044U, 4, 0x0000000dU);
            system->bus.write(0x04001200U, 4, 0xc8012010U); // LQV v1,0x100(zero).
            system->rsp.write_register(0x10, 1U);
        }
        compare(batched, stepped, 192); // Cache the block with no incoming vector dependency.
        CHECK(!batched.rsp.running());
        for (auto* system : {&batched, &stepped}) {
            system->rsp.write_pc(0x200U);
            system->rsp.write_register(0x10, 1U);
            system->rsp.tick(1U + age);
            system->rsp.write_pc(0);
        }
        // Stop on the last VADD's issue cycle. Reusing the earlier block cost
        // would incorrectly execute BREAK before this observation.
        compare(batched, stepped, budgets[age]);
        CHECK(batched.rsp.running());
        CHECK_EQ(batched.rsp.pc, 0x40U);
        compare(batched, stepped, 8);
        CHECK(!batched.rsp.running());
    }
}

TEST(rsp_local_blocks_preserve_branch_target_single_issue_restriction) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped}) {
        system->bus.write(0x04001004U, 4, 0x4a010850U); // VADD v1,v1,v1.
        system->bus.write(0x04001008U, 4, 0x24010001U); // ADDIU at,zero,1; eligible partner.
        system->bus.write(0x0400100cU, 4, 0x8c020100U); // LW v0,0x100(zero).
        system->bus.write(0x04001010U, 4, 0x00421821U); // ADDU v1,v0,v0; two operand bubbles.
        system->bus.write(0x04001044U, 4, 0x0000000dU); // BREAK after sixteen instructions.
        system->bus.write(0x04001200U, 4, 0x1000ff80U); // BEQ zero,zero,0x04.
        system->rsp.write_pc(4U);
        system->rsp.write_register(0x10, 1U);
    }
    compare(batched, stepped, 192); // Cache the entry with pairing enabled.
    CHECK(!batched.rsp.running());
    for (auto* system : {&batched, &stepped}) {
        system->rsp.write_pc(0x200U);
        system->rsp.write_register(0x10, 1U);
        system->rsp.tick(3); // Taken branch, delay slot, bubble; the unaligned target issues alone.
    }
    compare(batched, stepped, 27);
    CHECK(batched.rsp.running());
    CHECK_EQ(batched.rsp.pc, 0x44U);
    compare(batched, stepped, 8);
    CHECK(!batched.rsp.running());
}

TEST(rsp_local_blocks_cache_scalar_shift_arithmetic_and_logical_operands) {
    constexpr std::array program{
        immediate(0x09, 0, 1, -8),     // ADDIU at,zero,-8.
        immediate(0x09, 0, 2, 3),      // ADDIU v0,zero,3.
        special(0, 2, 3, 4, 0x00),     // SLL v1,v0,4.
        special(0, 3, 4, 2, 0x02),     // SRL a0,v1,2.
        special(0, 1, 5, 2, 0x03),     // SRA a1,at,2.
        special(2, 2, 6, 0, 0x04),     // SLLV a2,v0,v0.
        special(2, 3, 7, 0, 0x06),     // SRLV a3,v1,v0.
        special(2, 1, 8, 0, 0x07),     // SRAV t0,at,v0.
        special(3, 2, 3, 0, 0x21),     // ADDU v1,v1,v0; destination aliases a source.
        special(3, 4, 4, 0, 0x23),     // SUBU a0,v1,a0; destination aliases a source.
        special(3, 4, 9, 0, 0x24),     // AND t1,v1,a0.
        special(3, 4, 10, 0, 0x25),    // OR t2,v1,a0.
        special(3, 4, 11, 0, 0x26),    // XOR t3,v1,a0.
        special(3, 4, 12, 0, 0x27),    // NOR t4,v1,a0.
        special(1, 2, 13, 0, 0x2a),    // SLT t5,at,v0.
        special(1, 2, 14, 0, 0x2b),    // SLTU t6,at,v0.
        immediate(0x2b, 0, 3, 0x100),  // SW v1,0x100(zero).
        immediate(0x2b, 0, 4, 0x104),  // SW a0,0x104(zero).
        immediate(0x2b, 0, 6, 0x108),  // SW a2,0x108(zero).
        immediate(0x2b, 0, 7, 0x10c),  // SW a3,0x10c(zero).
        immediate(0x2b, 0, 8, 0x110),  // SW t0,0x110(zero).
        immediate(0x2b, 0, 9, 0x114),  // SW t1,0x114(zero).
        immediate(0x2b, 0, 10, 0x118), // SW t2,0x118(zero).
        immediate(0x2b, 0, 11, 0x11c), // SW t3,0x11c(zero).
        immediate(0x2b, 0, 12, 0x120), // SW t4,0x120(zero).
        immediate(0x2b, 0, 13, 0x124), // SW t5,0x124(zero).
        immediate(0x2b, 0, 14, 0x128), // SW t6,0x128(zero).
        0x0000000dU,                   // BREAK.
    };
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped})
        prepare_scalar_program(*system, program);

    compare(batched, stepped, 256);
    CHECK(!batched.rsp.running());
    CHECK_EQ(batched.bus.read(0x04000100U, 4), 0x33U);
    CHECK_EQ(batched.bus.read(0x04000104U, 4), 0x27U);
    CHECK_EQ(batched.bus.read(0x04000108U, 4), 0x18U);
    CHECK_EQ(batched.bus.read(0x0400010cU, 4), 0x06U);
    CHECK_EQ(batched.bus.read(0x04000110U, 4), 0xffffffffU);
    CHECK_EQ(batched.bus.read(0x04000114U, 4), 0x23U);
    CHECK_EQ(batched.bus.read(0x04000118U, 4), 0x37U);
    CHECK_EQ(batched.bus.read(0x0400011cU, 4), 0x14U);
    CHECK_EQ(batched.bus.read(0x04000120U, 4), 0xffffffc8U);
    CHECK_EQ(batched.bus.read(0x04000124U, 4), 1U);
    CHECK_EQ(batched.bus.read(0x04000128U, 4), 0U);
}

TEST(rsp_local_blocks_cache_signed_immediates_and_wrapped_dmem_operands) {
    constexpr std::array program{
        immediate(0x09, 0, 1, -1),     // ADDIU at,zero,-1.
        immediate(0x0c, 1, 1, 0x0fff), // ANDI at,at,0xfff; destination aliases source.
        immediate(0x0f, 0, 2, 0x80ff), // LUI v0,0x80ff.
        immediate(0x0d, 2, 2, 0x7f01), // ORI v0,v0,0x7f01; destination aliases source.
        immediate(0x2b, 1, 2, 0),      // SW v0,0(at); crosses the DMEM wrap.
        immediate(0x20, 1, 3, 0),      // LB v1,0(at).
        immediate(0x24, 1, 4, 1),      // LBU a0,1(at); wraps to address zero.
        immediate(0x21, 1, 5, 0),      // LH a1,0(at); crosses the DMEM wrap.
        immediate(0x25, 1, 6, 1),      // LHU a2,1(at).
        immediate(0x23, 1, 7, 0),      // LW a3,0(at); crosses the DMEM wrap.
        immediate(0x09, 5, 8, -1),     // ADDIU t0,a1,-1.
        immediate(0x0a, 8, 9, -128),   // SLTI t1,t0,-128.
        immediate(0x0b, 8, 10, -128),  // SLTIU t2,t0,-128; immediate remains sign-extended.
        immediate(0x0e, 6, 11, -1),    // XORI t3,a2,0xffff; logical immediate stays zero-extended.
        immediate(0x28, 1, 11, -1),    // SB t3,-1(at).
        immediate(0x29, 1, 6, -2),     // SH a2,-2(at).
        immediate(0x2b, 0, 3, 0x100),  // Store observed load results.
        immediate(0x2b, 0, 4, 0x104),
        immediate(0x2b, 0, 5, 0x108),
        immediate(0x2b, 0, 6, 0x10c),
        immediate(0x2b, 0, 7, 0x110),
        immediate(0x2b, 0, 8, 0x114),
        immediate(0x2b, 0, 9, 0x118),
        immediate(0x2b, 0, 10, 0x11c),
        immediate(0x2b, 0, 11, 0x120),
        immediate(0x23, 1, 1, 0), // LW at,0(at); load destination aliases its base.
        immediate(0x2b, 0, 1, 0x124),
        0x0000000dU, // BREAK.
    };
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped})
        prepare_scalar_program(*system, program);

    compare(batched, stepped, 256);
    CHECK(!batched.rsp.running());
    CHECK_EQ(batched.bus.read(0x04000100U, 4), 0xffffff80U);
    CHECK_EQ(batched.bus.read(0x04000104U, 4), 0xffU);
    CHECK_EQ(batched.bus.read(0x04000108U, 4), 0xffff80ffU);
    CHECK_EQ(batched.bus.read(0x0400010cU, 4), 0xff7fU);
    CHECK_EQ(batched.bus.read(0x04000110U, 4), 0x80ff7f01U);
    CHECK_EQ(batched.bus.read(0x04000114U, 4), 0xffff80feU);
    CHECK_EQ(batched.bus.read(0x04000118U, 4), 1U);
    CHECK_EQ(batched.bus.read(0x0400011cU, 4), 1U);
    CHECK_EQ(batched.bus.read(0x04000120U, 4), 0x80U);
    CHECK_EQ(batched.bus.read(0x04000124U, 4), 0x80ff7f01U);
    CHECK_EQ(batched.bus.read(0x04000ffdU, 2), 0xff7fU);
    CHECK_EQ(batched.bus.read(0x04000fffU, 1), 0x80U);
    CHECK_EQ(batched.bus.read(0x04000000U, 2), 0xff7fU);
}
